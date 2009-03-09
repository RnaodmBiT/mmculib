/** @file   flashheap.c
    @author Michael Hayes
    @date   23 February 2009
    @brief  Routines to implement a heap in a dataflash memory
            with wear-levelling.
*/

#include <flashheap.h>
#include <stdlib.h>

/* The goals of this code is to implement a heap within flash memory
   and to minimise writes.  Some flash devices have a more robust first block
   but we don't assume this.
*/


typedef struct
{
    flashheap_size_t size;
} flashheap_packet_t;


/* Return true if desire to terminate walk.  */
typedef bool
(*flashheap_callback_t)(flashheap_addr_t addr,
                        flashheap_packet_t *packet, void *arg);


static flashheap_dev_t heap_data;


static bool
flashheap_packet_read (flashheap_t heap, flashheap_addr_t addr, 
                       flashheap_packet_t *ppacket)
{
    return heap->read (heap->dev, addr, ppacket, sizeof (*ppacket))
        == sizeof (*ppacket);
}


bool
flashheap_free (flashheap_t heap, void *ptr)
{
    flashheap_packet_t packet;
    flashheap_packet_t prev_packet;
    flashheap_packet_t next_packet;
    flashheap_addr_t prev_addr;
    flashheap_addr_t next_addr;
    flashheap_addr_t addr;
    flashheap_addr_t desired;

    desired = (flashheap_addr_t)ptr;
    prev_addr = 0;
    addr = heap->offset;
    prev_packet.size = 0;

    /* Linear search through heap looking for desired packet.  */
    while (addr < heap->offset + heap->size)
    {
        if (!flashheap_packet_read (heap, addr, &packet))
            return 0;

        if (addr == desired)
            break;
        
        prev_addr = addr;
        prev_packet = packet;
        addr += abs (packet.size) + sizeof (packet);
    }

    /* Couldn't find packet.  */
    if (addr != desired)
        return 0;

    if (packet.size < 0)
        return 0;
    packet.size = -packet.size;

    next_addr = addr + abs (packet.size) + sizeof (packet);
    if (!flashheap_packet_read (heap, next_addr, &next_packet))
        return 0;
    
    if (prev_packet.size < 0 && next_packet.size < 0)
    {
        /* Coalesce previous, current, and next packets.  */
        packet.size += prev_packet.size + next_packet.size
            - 2 * sizeof (packet);
        addr = prev_addr;
    }
    else if (next_packet.size < 0)
    {
        /* Coalesce current and next packets.  */
        packet.size += next_packet.size - sizeof (packet);
    }
    else if (prev_packet.size < 0)
    {
        /* Coalesce current and prev packets.  */
        packet.size += prev_packet.size - sizeof (packet);
        addr = prev_addr;
    }

    if (heap->write (heap->dev, addr, &packet,
                     sizeof (packet)) != sizeof (packet))
        return 0;

    return 1;
}


void *
flashheap_alloc (flashheap_t heap, flashheap_size_t size)
{
    flashheap_packet_t packet;
    flashheap_addr_t addr;

    addr = heap->offset;

    while (addr < heap->offset + heap->size)
    {
        if (!flashheap_packet_read (heap, addr, &packet))
            return 0;
        
        if (packet.size < 0 && size <= -packet.size)
        {
            /* Have a free packet so need to split.  */
            if (size != -packet.size)
            {
                flashheap_packet_t new_packet;
                flashheap_addr_t new_addr;

                new_addr = addr + sizeof (packet) + size;
                new_packet.size = -(-packet.size - size - sizeof (packet));

                if (heap->write (heap->dev, new_addr, &new_packet,
                                 sizeof (new_packet)) != sizeof (new_packet))
                    return 0;
            }

            packet.size = size;
            if (heap->write (heap->dev, addr, &packet,
                             sizeof (packet)) != sizeof (packet))
                return 0;            

            heap->last = addr;
            return (void *)addr;
        }
        /* Skip to start of next packet.  */
        addr += abs (packet.size) + sizeof (packet);
    }

    /* No free packet.  */
    return 0;
}


/* Iterate over packets starting at addr.  Stop if callback returns true.  */
static bool
flashheap_walk (flashheap_t heap, flashheap_addr_t addr, 
                flashheap_callback_t callback, void *arg)
{
    flashheap_packet_t packet;

    for (; addr < heap->offset + heap->size;
         addr += abs (packet.size) + sizeof (packet))
    {
        if (!flashheap_packet_read (heap, addr, &packet))
            return 0;

        if (callback (addr, &packet, arg))
            return 1;
    }
    return 0;
}


static bool
flashheap_alloc_p (flashheap_addr_t addr, flashheap_packet_t *ppacket, 
                   void *arg)
{
    flashheap_addr_t *paddr = arg;

    *paddr = addr;
    return ppacket->size >= 0;
}


void *
flashheap_alloc_first (flashheap_t heap)
{
    flashheap_addr_t addr;

    if (flashheap_walk (heap, heap->offset, flashheap_alloc_p, &addr))
        return (void *)addr;
        
    return 0;
}


void *
flashheap_alloc_next (flashheap_t heap, void *ptr)
{
    flashheap_addr_t addr;
    flashheap_packet_t packet;

    addr = (flashheap_addr_t) ptr;

    if (!addr)
        return flashheap_alloc_first (heap);
    
    if (!flashheap_packet_read (heap, addr, &packet))
        return 0;

    addr += abs (packet.size);

    if (flashheap_walk (heap, addr, flashheap_alloc_p, &addr))
        return (void *)addr;

    return 0;
}


flashheap_size_t
flashheap_alloc_size (flashheap_t heap, void *ptr)
{
    flashheap_packet_t packet;
    flashheap_addr_t addr;

    addr = (flashheap_addr_t) ptr;
    
    if (!flashheap_packet_read (heap, addr, &packet))
        return 0;
    
    if (packet.size < 0)
        return 0;
    
    return packet.size;
}


static bool
flashheap_stats_helper (flashheap_addr_t addr,
                        flashheap_packet_t *ppacket, 
                        void *arg)
{
    flashheap_stats_t *pstats = arg;
    
    if (ppacket->size >= 0)
    {
        pstats->alloc_packets++;
        pstats->alloc_bytes += ppacket->size;
    }
    else
    {
        pstats->free_packets++;
        pstats->free_bytes -= ppacket->size;
    }
    return 0;
}


void
flashheap_stats (flashheap_t heap, flashheap_stats_t *pstats)
{
    pstats->alloc_packets = 0;
    pstats->free_packets = 0;
    pstats->alloc_bytes = 0;
    pstats->free_bytes = 0;

    flashheap_walk (heap, heap->offset, flashheap_stats_helper, pstats);
}


bool
flashheap_erase (flashheap_t heap)
{
    flashheap_packet_t packet;

    /* Create one large empty packet.  */
    packet.size = -(heap->size - sizeof (packet));

    if (heap->write (heap->dev, heap->offset, &packet,
                     sizeof (packet)) != sizeof (packet))
        return 0;

    return 1;
}


flashheap_t
flashheap_init (flashheap_addr_t offset, flashheap_size_t size,
                void *dev, flashheap_read_t read,
                flashheap_write_t write)
{
    flashheap_t heap;

    /* Note offset cannot be zero.  */

    heap = &heap_data;
    heap->dev = dev;
    heap->read = read;
    heap->write = write;
    heap->offset = offset;
    heap->size = size;

    return heap;
}