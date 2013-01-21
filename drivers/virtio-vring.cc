#include <string.h>
#include "mempool.hh"
#include "mmu.hh"
#include "kern/sglist.hh"
#include "barrier.hh"

#include "drivers/virtio.hh"
#include "drivers/virtio-vring.hh"
#include "debug.hh"

using namespace memory;

namespace virtio {

    vring::vring(virtio_driver* const drv, u16 num, u16 q_index)
    {
        _drv = drv;
        _q_index = q_index;
        // Alloc enough pages for the vring...
        unsigned sz = VIRTIO_ALIGN(vring::get_size(num, VIRTIO_PCI_VRING_ALIGN));
        _vring_ptr = malloc(sz);
        memset(_vring_ptr, 0, sz);
        
        // Set up pointers        
        _num = num;
        _desc = (vring_desc *)_vring_ptr;
        _avail = (vring_avail *)(_vring_ptr + num*sizeof(vring_desc));
        _used = (vring_used *)(((unsigned long)&_avail->_ring[num] + 
                sizeof(u16) + VIRTIO_PCI_VRING_ALIGN-1) & ~(VIRTIO_PCI_VRING_ALIGN-1));

        // initialize the next pointer within the available ring
        for (int i=0;i<num;i++) _avail->_ring[i] = i+1;
    }

    vring::~vring()
    {
        free(_vring_ptr);
    }

    u64 vring::get_paddr(void)
    {
        return mmu::virt_to_phys(_vring_ptr);
    }

    unsigned vring::get_size(unsigned int num, unsigned long align)
    {
        return (((sizeof(vring_desc) * num + sizeof(u16) * (3 + num)
                 + align - 1) & ~(align - 1))
                + sizeof(u16) * 3 + sizeof(vring_used_elem) * num);
    }

    int vring::need_event(u16 event_idx, u16 new_idx, u16 old)
    {
        // Note: Xen has similar logic for notification hold-off
        // in include/xen/interface/io/ring.h with req_event and req_prod
        // corresponding to event_idx + 1 and new_idx respectively.
        // Note also that req_event and req_prod in Xen start at 1,
        // event indexes in virtio start at 0.
        return ( (u16)(new_idx - event_idx - 1) < (u16)(new_idx - old) );
    }

    bool
    vring::add_buf(sglist* sg, u16 out, u16 in, void* cookie) {
        //if (_avail->count() < (in+out)) {
            // what should I do?
        //}

        int i = 0;
        vring_desc* desc = &_desc[_avail->_ring[_avail->_idx]];
        for (auto ii = sg->_nodes.begin();i<in+out;ii++) {
            desc->_flags = vring_desc::VRING_DESC_F_NEXT | (i>in)? vring_desc::VRING_DESC_F_WRITE:0;
            desc->_paddr = (*ii)._paddr;
            desc->_len = (*ii)._len;
            desc->_next = _avail->_ring[_avail->_idx];
            _avail->_idx++;
            desc = &_desc[_avail->_ring[desc->_next]];
            i++;
        }
        desc->_flags &= ~vring_desc::VRING_DESC_F_NEXT;



        //_avail->add(sg, in, out, cookie);
        //used idx math

        return true;
    }

    void*
    vring::get_buf(int* len) {
        return nullptr;
    }

    bool
    vring::kick() {
        _drv->kick(_q_index);
        return true;
    }

    void
    vring::disable_callback() {

    }

    bool
    vring::enable_callback() {
        return true;
    }


};