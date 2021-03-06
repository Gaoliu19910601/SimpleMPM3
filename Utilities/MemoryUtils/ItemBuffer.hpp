#ifndef __Meomory_utils_Item_Buffer_hpp__
#define __Meomory_utils_Item_Buffer_hpp__

#include <string>

// must be power of two
#define MEMORY_ALIGNMENT sizeof(void *)

// padding to round up the cloest power of two
#define MEMORY_ALIGNMENT_PADDING(address) \
	((MEMORY_ALIGNMENT - ((address) & (MEMORY_ALIGNMENT - 1))) & (MEMORY_ALIGNMENT - 1))

// round up the cloest power of two
#define MEMORY_ALIGNED_ADDRESS(address) \
	((address) + MEMORY_ALIGNMENT_PADDING(address))

namespace MemoryUtils
{
	/*===========================================================
	Class ItemBuffer
	-------------------------------------------------------------
	1. Store items without sequence;
	2. Need reasonable page size for efficiency;
	3. del() has no check to ensure the item is in this buffer.
	============================================================*/
	template<typename Item, size_t fold = 1, size_t pre_alloc_size = 0>
	class ItemBuffer
	{
	protected: // assistant data structures
		union ItemSlot
		{
			char item[sizeof(Item)];
			ItemSlot *next;
		};
		struct MemPageHeader
		{
			MemPageHeader *next;
			ItemSlot *start;
			size_t size;
		};

	protected:
		ItemSlot *empty_slot; // list of empty slot
		union // in-stack memory page
		{
			MemPageHeader in_stack_page;
			struct
			{
				MemPageHeader *in_stack_page_next;
				ItemSlot *in_stack_page_start;
				size_t in_stack_page_size;
			};
			char in_stack_page_mem[sizeof(MemPageHeader) +
								   sizeof(ItemSlot) * pre_alloc_size +
								   MEMORY_ALIGNMENT * (pre_alloc_size ? 1 : 0)];
		};
		size_t base_page_size, page_size;
		MemPageHeader *cur_page; 
		ItemSlot *cur, *end;
		
	public:
		ItemBuffer(size_t init_page_size = pre_alloc_size) :
			empty_slot(nullptr),
			in_stack_page_next(nullptr),
			in_stack_page_start((ItemSlot *)MEMORY_ALIGNED_ADDRESS(size_t(in_stack_page_mem) + sizeof(MemPageHeader))),
			in_stack_page_size(pre_alloc_size),
			base_page_size(init_page_size ? init_page_size : 1),
			page_size(base_page_size),
			cur_page(&in_stack_page),
			cur(in_stack_page_start),
			end(in_stack_page_start + in_stack_page_size) {}
		~ItemBuffer() { clear(); }
		inline void set_page_size(size_t init_page_size) noexcept
		{
			base_page_size = init_page_size ? init_page_size : 1;
			page_size = base_page_size;
		}
		inline size_t get_page_size() const noexcept { return page_size; }
		// allocate memory for single item
		inline Item *alloc()
		{
			if (empty_slot)
			{
				Item *res = (Item *)empty_slot;
				empty_slot = empty_slot->next;
				return res;
			}
			else if (cur < end)
				return (Item *)(cur++);
			move_to_next_page();
			return (Item *)(cur++);
		}
		inline Item *alloc(size_t num)
		{
			Item *res = (Item *)cur;
			cur += num;
			if (cur > end)
			{
				// add unused slot back to unused list
				if ((ItemSlot *)res < end)
				{
					ItemSlot *last = end - 1;
					last->next = empty_slot;
					for (cur = (ItemSlot *)res; cur < last; ++cur)
						cur->next = cur + 1;
					empty_slot = (ItemSlot *)res;
				}
				// get next page for allocation
				move_to_next_page(num);
				res = (Item *)cur;
				cur += num;
			}
			return res;
		}
		// may cause loss of memory in exchange for higher speed
		inline Item *alloc_fast(size_t num)
		{
			Item *res = (Item *)cur;
			cur += num;
			if (cur > end)
			{
				move_to_next_page(num);
				res = (Item *)cur;
				cur += num;
			}
			return res;
		}
		inline void del(Item *pitem)
		{
			ItemSlot *tmp = (ItemSlot *)(pitem);
			tmp->next = empty_slot;
			empty_slot = tmp;
		}
		inline void del(Item *pitems, size_t num)
		{
			ItemSlot *slots = (ItemSlot *)(pitems);
			slots[--num].next = empty_slot;
			for (size_t i = 0; i < num; i++)
				slots[i].next = &slots[i + 1];
			empty_slot = slots;
		}
		inline void reset()
		{
			empty_slot = nullptr;
			cur_page = &in_stack_page;
			cur = in_stack_page_start;
			end = in_stack_page_start + in_stack_page_size;
		}
		void clear()
		{
			// reset page size
			page_size = base_page_size;
			// clear list
			MemPageHeader *&top_alloc_page = in_stack_page.next;
			MemPageHeader *tmp_page;
			while (top_alloc_page)
			{
				tmp_page = top_alloc_page;
				top_alloc_page = top_alloc_page->next;
				delete[] (char*)tmp_page;
			}
			in_stack_page.next = nullptr;
			in_stack_page_start = ((ItemSlot *)MEMORY_ALIGNED_ADDRESS(size_t(in_stack_page_mem) + sizeof(MemPageHeader)));
			in_stack_page_size = pre_alloc_size;
			// reset state
			empty_slot = nullptr;
			cur_page = &in_stack_page;
			cur = in_stack_page_start;
			end = cur + in_stack_page_size;
		}
		// Reset and contract all memory pages into one (if necessary)
		inline void reset_and_optimize()
		{
			if (cur_page == &in_stack_page)
			{
				empty_slot = nullptr;
				cur = in_stack_page_start;
				return;
			}
			_reset_and_optimize();
		}
		
	protected:
		// get the next memory pool with size > min_size_required
		void move_to_next_page(size_t min_size_required = 1)
		{
			while (cur_page->next)
			{
				// move the next page
				cur_page = cur_page->next;
				if (cur_page->size < min_size_required)
					continue;
				cur = cur_page->start;
				end = cur + cur_page->size;
				return;
			}
			if (page_size < min_size_required)
				page_size = min_size_required;
			union { char *mem; MemPageHeader *mem_page; };
			// alloc new page (if necessary)
			mem = new char[sizeof(MemPageHeader) + page_size * sizeof(ItemSlot) + MEMORY_ALIGNMENT];
			// init new page
			mem_page->next = nullptr;
			mem_page->start = (ItemSlot *)MEMORY_ALIGNED_ADDRESS(size_t(mem) + sizeof(MemPageHeader));
			mem_page->size = page_size;
			// add to list
			cur_page->next = mem_page;
			cur_page = mem_page;
			cur = cur_page->start;
			end = cur + page_size;
			// update page_size
			page_size *= fold;
		}
		void _reset_and_optimize()
		{
			// Calculate total allocated size
			size_t total_size = 0;
			for (MemPageHeader *page_iter = &in_stack_page; 
				 page_iter; page_iter = page_iter->next)
				total_size += page_iter->size;
			// clear all old pages
			MemPageHeader *&top_alloc_page = in_stack_page.next;
			MemPageHeader *tmp_page;
			while (top_alloc_page)
			{
				tmp_page = top_alloc_page;
				top_alloc_page = top_alloc_page->next;
				delete[] (char*)tmp_page;
			}
			// alloc one single page
			union { char *mem; MemPageHeader *mem_page; };
			mem = new char[sizeof(MemPageHeader) + sizeof(ItemSlot) * total_size + MEMORY_ALIGNMENT];
			// abandon original in-stack space
			in_stack_page_next = mem_page;
			in_stack_page_start = (ItemSlot *)MEMORY_ALIGNED_ADDRESS(size_t(mem) + sizeof(MemPageHeader));
			in_stack_page_size = total_size;
			mem_page->next = nullptr;
			mem_page->start = nullptr; // no need to calculate
			mem_page->size = 0;
			// reset state
			empty_slot = nullptr;
			cur_page = &in_stack_page;
			cur = in_stack_page_start;
			end = in_stack_page_start + in_stack_page_size;
		}
	};
};

#undef MEMORY_ALIGNMENT
#undef MEMORY_ALIGNMENT_PADDING
#undef MEMORY_ALIGNED_ADDRESS

#endif