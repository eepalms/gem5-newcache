/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Erik Hallnor
 */

/**
 * @file
 * Definitions of LRU tag store.
 */

#include <inttypes.h>

#include <string>

#include "base/intmath.hh"
#include "base/random.hh"
#include "debug/CacheRepl.hh"
#include "mem/cache/tags/newcache.hh"
#include "mem/cache/base.hh"
#include "sim/core.hh"

using namespace std;

// create and initialize a NEWCACHE cache structure
NEWCACHE::NEWCACHE(const Params *p)
         :BaseTags(p), assoc(p->assoc),
          numSets(p->size / (p->block_size * p->assoc)),
          nebit(p->nebit)
{
    // Check parameters
    if (blkSize < 4 || !isPowerOf2(blkSize)) {
        fatal("Block size must be at least 4 and a power of 2");
    }
    if (numSets <= 0 || !isPowerOf2(numSets)) {
        fatal("# of sets must be non-zero and a power of 2");
    }
    assert(numSets == 1);
    if (assoc <= 0){
        fatal("associativity must be greater than zero");
     }
    if (hitLatency <= 0) {
        fatal("access latency must be greater than zero");
     }
    if (nebit < 0) {
        fatal("number of extra index bits must be greater than zero");
    }

    blkMask = blkSize - 1;
    setShift = floorLog2(blkSize);
    setMask = numSets - 1;
    tagShift = setShift + floorLog2(numSets*assoc) + nebit;
    diMask = (1 << (floorLog2(numSets*assoc)+nebit))-1;
    warmedUp = false;
    /** @todo Make warmup percentage a parameter. */
    warmupBound = numSets * assoc;

    sets = new SetType[numSets];
    blks = new BlkType[numSets * assoc];
    // allocate data storage in one big chunk
    numBlocks = numSets * assoc;
    dataBlks = new uint8_t[numBlocks * blkSize];

    unsigned blkIndex = 0;       // index into blks array
    for (unsigned i = 0; i < numSets; ++i) {
        sets[i].assoc = assoc;

        sets[i].blks = new BlkType*[assoc];

        // link in the data blocks
        for (unsigned j = 0; j < assoc; ++j) {
            // locate next cache block
            BlkType *blk = &blks[blkIndex];
            blk->data = &dataBlks[blkSize*blkIndex];
            ++blkIndex;

            // invalidate new cache block
            blk->invalidate();

            //EGH Fix Me : do we need to initialize blk?

            // Setting the tag to j is just to prevent long chains in the hash
            // table; won't matter because the block is invalid
            blk->tag = j;
            // initialize lnreg
            blk->lnreg = 0;
            blk->P_bit = -1;
            blk->rmtid = -1;
            blk->whenReady = 0;
            blk->isTouched = false;
            blk->size = blkSize;
            sets[i].blks[j]=blk;
            blk->set = i;
        }
    }
}

NEWCACHE::~NEWCACHE()
{
    delete [] dataBlks;
    delete [] blks;
    delete [] sets;
}

NEWCACHE::BlkType*
NEWCACHE::accessBlock(PacketPtr pkt, Cycles &lat, int master_id)
{
    Addr addr = pkt->getAddr();
    Addr tag = extractTag(addr);
    unsigned set = extractSet(addr);
    int lnreg = extractDI(addr);
    int rmtid = pkt->req->getRmtid(); 
    int P_bit = 0;
    if (pkt->req->isProtect())
       P_bit = 1;
    // kernel is shared by all the processes and the global translation has a default rmt_id (0)
    if (pkt->req->isGlobal())
       rmtid = 0;
    BlkType *blk = NULL;
    blk = sets[set].mapLookup(rmtid, P_bit, lnreg);
    if (blk && blk->isValid() && (blk->tag != tag)){
       // tag miss
       blk = NULL;
    }
    lat = hitLatency;
    if (blk != NULL) {
        DPRINTF(CacheRepl, "set %x: moving blk %x to MRU\n",
                set, regenerateBlkAddr(tag, set, lnreg));
        if (blk->whenReady > curTick()
            && cache->ticksToCycles(blk->whenReady - curTick()) > hitLatency) {
            lat = cache->ticksToCycles(blk->whenReady - curTick());
        }
        blk->refCount += 1;
    }

    return blk;
}


NEWCACHE::BlkType*
NEWCACHE::findBlock(PacketPtr pkt) const
{
    Addr addr = pkt->getAddr();
    Addr tag = extractTag(addr);
    unsigned set = extractSet(addr);
    unsigned lnreg = extractDI(addr);
    BlkType *blk = NULL;
    int rmtid = pkt->req->getRmtid();
    int P_bit = 0;
    if (pkt->req->isProtect())
       P_bit = 1;
    if (pkt->req->isGlobal())
       rmtid = 0;

    blk = sets[set].findBlk(rmtid, P_bit, lnreg, tag);
    if (blk && blk->isValid() && (blk->tag != tag)){
       // tag miss
       blk = NULL;
    }

    return blk;
}


NEWCACHE::BlkType*
NEWCACHE::findVictim(PacketPtr pkt, PacketList &writebacks)
{
    Addr addr = pkt->getAddr();
    unsigned set = extractSet(addr);
    unsigned lnreg = extractDI(addr);
    Addr tag = extractTag(addr);
    BlkType *blk = NULL;
    BlkType *tmpblk = NULL;
    uint16_t rpl;
    int rmtid = pkt->req->getRmtid();
    int P_bit = 0;
    if (pkt->req->isProtect())
       P_bit = 1;
    if (pkt->req->isGlobal())
       rmtid = 0;

    // grab a replacement candidate
     tmpblk = sets[set].mapLookup(rmtid, P_bit, lnreg);
     if (tmpblk){
          assert(tmpblk->tag != tag);
          blk = tmpblk;
     }
     else {
     rpl = random_mt.random<uint64_t>(0,assoc-1);
     blk = sets[set].blks[rpl];

     }

    return blk;
}

void
NEWCACHE::insertBlock(PacketPtr pkt, BlkType *blk)
{
    Addr addr = pkt->getAddr();
    MasterID master_id = pkt->req->masterId();
    Addr tag = extractTag(addr);
    unsigned lnreg = extractDI(addr);
    unsigned set = extractSet(addr);
  
    int rmtid = pkt->req->getRmtid();
    int P_bit = 0;
    if (pkt->req->isProtect())
       P_bit = 1;
    if (pkt->req->isGlobal())
       rmtid = 0;

   // don't change the order of the code, make sure not invalidate block before inserting
   // Set tag for new block.  Caller is responsible for setting status.
   // tag miss
    if ((blk->rmtid == rmtid) && (blk->P_bit==P_bit) && (blk->lnreg==lnreg) && blk->isValid()){
        blk->tag = tag;
        blk->rmtid = rmtid;
        assert(sets[set].inMap(blk->rmtid, blk->P_bit, blk->lnreg));
     } else {
        //erase the mapping
        if (blk->isValid() && sets[set].inMap(blk->rmtid, blk->P_bit, blk->lnreg)){
           sets[set].lnregMap.erase(sets[set].setKey(blk->rmtid, blk->P_bit, blk->lnreg));
         }
           // update lnreg and hash table
           blk->tag = tag;
           blk->P_bit = P_bit;
           blk->lnreg = lnreg;
           blk->rmtid = rmtid;
           sets[set].lnregMap[sets[set].setKey(rmtid, P_bit, lnreg)] = blk;
       }

    if (!blk->isTouched) {
        tagsInUse++;
        blk->isTouched = true;
        if (!warmedUp && tagsInUse.value() >= warmupBound) {
            warmedUp = true;
            warmupCycle = curTick();
        }
    }

     // If we're replacing a block that was previously valid update
    // stats for it. This can't be done in findBlock() because a
    // found block might not actually be replaced there if the
    // coherence protocol says it can't be.
    if (blk->isValid()) {
        replacements[0]++;
        totalRefs += blk->refCount;
        ++sampledRefs;
        blk->refCount = 0;
        // deal with evicted block
        assert(blk->srcMasterId < cache->system->maxMasters());
        occupancies[blk->srcMasterId]--;

        blk->invalidate();
    }

    blk->isTouched = true;

    // deal with what we are bringing in
    assert(master_id < cache->system->maxMasters());
    occupancies[master_id]++;
    blk->srcMasterId = master_id;

}

void
NEWCACHE::invalidate(BlkType *blk)
{
    assert(blk);
    assert(blk->isValid());
    tagsInUse--;
    assert(blk->srcMasterId < cache->system->maxMasters());
    occupancies[blk->srcMasterId]--;
    blk->srcMasterId = Request::invldMasterId;

      if (sets[blk->set].inMap(blk->rmtid, blk->P_bit, blk->lnreg))
          sets[blk->set].lnregMap.erase(sets[blk->set].setKey(blk->rmtid, blk->P_bit, blk->lnreg));
}

void
NEWCACHE::clearLocks()
{
    for (int i = 0; i < numBlocks; i++){
        blks[i].clearLoadLocks();
    }
}


NEWCACHE *
NEWCACHEParams::create()
{
    return new NEWCACHE(this);
}

std::string
NEWCACHE::print() const {
    std::string cache_state;
    for (unsigned i = 0; i < numSets; ++i) {
        // link in the data blocks
        for (unsigned j = 0; j < assoc; ++j) {
            BlkType *blk = sets[i].blks[j];
            if (blk->isValid())
                cache_state += csprintf("\tset: %d block: %d %s\n", i, j,
                        blk->print());
        }
    }
    if (cache_state.empty())
        cache_state = "no valid tags\n";
    return cache_state;
}


void
NEWCACHE::cleanupRefs()
{
    for (unsigned i = 0; i < numSets*assoc; ++i) {
        if (blks[i].isValid()) {
            totalRefs += blks[i].refCount;
            ++sampledRefs;
        }
    }
}

                    
