//////////////////////////////////////////////////////////////////////////////////
// Engineer: Guoci Chen <gcc314@stu.pku.edu.cn>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Flexible Data Placement
// File Name: fdp.c
//
// Version: v1.0.0
//
// Description:
//	 - TODO
//////////////////////////////////////////////////////////////////////////////////

#include "fdp.h"

//---------------------------------------
// FDP Configuration
//---------------------------------------

EnduranceGroup *endgrp;

/**
 * @brief Setup NVMe namespace configuration for FDP
 * 
 * @param void
 * @return void
 * 
 * @details
 * We construct a static configuration for NVMe namespace management there,
 * where FDP Placement Handlers should be specified **MANUALLY**.
 */
void FDPNVMeNSConfigSetup()
{
    endgrp->ns = (NamespaceFDP *)FDP_NS_ADDR;
    for(int i = 0; i < USER_CHANNELS; i++)
    {
        // TODO: Specific PH patterns
        endgrp->ns[i].fdp.nphs              = 2;
        for(int j = 0; j < endgrp->ns[i].fdp.nphs; j++)
        {
            endgrp->ns[i].fdp.phs[j]        = (i & 0x10) | (j & 0x01);
        }
    }
}


/**
 * @brief Setup FDP configuration
 * 
 * @param void
 * @return void
 * 
 * @details
 * We construct a static configuration for FDP there, given the params in "fdp.h".
 * NVMe namespace management is unavailable on the runtime, so we do it here.
 */
void FDPConfigSetup()
{
    if(FDP_CONF_NRUH * FDP_CONF_NRG >= (1 << 15)) {
        assert(!"NRUH * NRG should be less than 2^15.");
    }

    endgrp->fdp.nruh            = FDP_CONF_NRUH;
    endgrp->fdp.nrg             = FDP_CONF_NRG;
    endgrp->fdp.rgif            = FDP_CONF_RGIF;
    endgrp->fdp.runs            = FDP_CONF_RUNS;
    endgrp->fdp.hbmw            = 0;
    endgrp->fdp.mbmw            = 0;
    endgrp->fdp.mbe             = 0;
    endgrp->fdp.enabled         = FDP_CONF_ENABLED;

    endgrp->fdp.ruhs = (RUHandle *)FDP_RUHS_ADDR;
    for(int i = 0; i < FDP_CONF_NRUH; i++)
    {
        endgrp->fdp.ruhs[i].ruht = FDP_CONF_RUHT;
        endgrp->fdp.ruhs[i].ruha = 0;
        endgrp->fdp.ruhs[i].event_filter = 0;
        endgrp->fdp.ruhs[i].lbafi = 0;
        endgrp->fdp.ruhs[i].ruamw = 0;
    }

    FDPNVMeNSConfigSetup();
}

/**
 * @brief Initialize FDP memory
 * 
 * @param void
 * @return void
 * 
 * @details
 * This function is used to initialize FDP memory,
 * including Block RU Table, Reclaim Groups, etc.
 * 
 * Then, how to map the RU across the blocks?
 * The stategy should be manually specified in this function.
 * 
 * In our impl, we use a simple mapping strategy:
 * We let blocks in One RU be spreaded over different dies.
 */
void FDPMemInit()
{
    blockRUInfoTable = (BlockRUInfo *)FDP_BLOCKRUINFO_TABLE_ADDR;
    endgrp->rgs = (ReclaimGroup *)FDP_RG_ADDR;
    ReclaimGroup *rg;
    ReclaimUnit *ru;
    RUADDR_T ruAddr;
    BLOADDR_T bloAddr;
    unsigned int dieNo = 0, blockNo = 0;
    for(RGID_T rgId = 0; rgId < FDP_CONF_NRG; rgId++) {
        rg = &endgrp->rgs[rgId];
        rg->free_ru.head = 0;
        rg->free_ru.tail = FDP_C_RUCNT_PER_GROUP - 1;
        rg->free_ru_cnt = FDP_C_RUCNT_PER_GROUP;
        for(RUHID_T i = 0; i < FDP_CONF_NRUH; i++) {
            for(SLICEID_T j = 0; j <= FDP_C_SLICE_PER_RU; j++) {
                rg->victim_ru[i][j].head = RU_NONE;
                rg->victim_ru[i][j].tail = RU_NONE;
            }
        }
        for(RUGID_T rugId = 0; rugId < FDP_C_RUCNT_PER_GROUP; rugId++) {
            ru = &rg->rus[rugId];
            ru->ruamw = 0;
            ru->ruhid = RUHID_NONE;
            ru->current_slice = 0;
            ru->invalid_slices = 0;
            ru->prev_ru = rugId == 0 ? RU_NONE : rugId - 1;
            ru->next_ru = rugId == FDP_C_RUCNT_PER_GROUP - 1 ? RU_NONE : rugId + 1;

            // Mapping stategy
            ruAddr = (rugId << endgrp->fdp.rgif) | rgId;
            for(int i = 0; i < FDP_CONF_RUSIZE_BLOCKS; i++) {
                bloAddr = (dieNo << LOG_BLOCKS_PER_DIE) | blockNo;
                blockRUInfoTable->blocks[dieNo][blockNo].ru_addr = ruAddr;
                ru->blo_addr[i] = bloAddr;
                dieNo++;
                if(dieNo == USER_DIES) {
                    dieNo = 0;
                    blockNo++;
                }
            }
        }
    }
}


/**
 * @brief Initialize Reclaim Handlers
 * 
 * @param void
 * @return void
 * 
 * @details
 * Each RUH should initially link to a RU in each Reclaim Group.
 * We left that to be done now.
 * 
*/
void FDPInitRHs()
{
    ReclaimGroup *rg;
    RUHandle *ruh;
    for(RUHID_T ruhId = 0; ruhId < FDP_CONF_NRUH; ruhId++)
    {
        ruh = &endgrp->fdp.ruhs[ruhId];
        for(RGID_T rgId = 0; rgId < FDP_CONF_NRG; rgId++)
        {
            rg = &endgrp->rgs[rgId];
            RUGID_T rugId = getFreeRU(rgId, GET_FREE_RU_FOR_USE);
            ruh->rus[rgId] = rugId;
            rg->rus[rugId].ruhid = ruhId;
        }
    }
}

/**
 * @brief Initialize FDP
 * 
 * @param void
 * @return void
 * 
 * @details
 * This function is used to initialize FDP.
 */
void FDPInit()
{
    endgrp = (EnduranceGroup *) FDP_ENDGRP_ADDR;
    FDPConfigSetup();   
    FDPMemInit();
    FDPInitRHs();
}

/**
 * @brief Get a free RU
 * 
 * @param rgId Reclaim Group ID
 * @param getMode Get Mode
 *      - GET_FREE_RU_FOR_USE: Reserve some free RUs when normal write, for the sake of GC
 *      - GET_FREE_RU_FOR_GC: Can use all free RUs when GC
 * 
 * @return Free RU ID
 */
unsigned short getFreeRU(RGID_T rgId, char getMode)
{
    ReclaimGroup *rg = &endgrp->rgs[rgId];
    RUGID_T rugId = rg->free_ru.head;
    if(getMode == GET_FREE_RU_FOR_USE)
    {
        // should reserve some free RUs
        if(rg->free_ru_cnt <= RESERVED_FREE_RU_CNT)
            return RU_NONE;
    }
    else
    {
        // for GC, can use all free RUs
        if(rugId == RU_NONE)
            return RU_NONE;
    }

    ReclaimUnit *ru = &rg->rus[rugId];
    if(ru->next_ru == RU_NONE)
    {
        rg->free_ru.head = RU_NONE;
        rg->free_ru.tail = RU_NONE;
    }
    else
    {
        rg->free_ru.head = ru->next_ru;
        rg->rus[ru->next_ru].prev_ru = RU_NONE;
    }
    rg->free_ru_cnt--;
    ru->prev_ru = RU_NONE;
    ru->next_ru = RU_NONE;
    return rugId;
}

/**
 * @brief Put a free RU
 * 
 * @param rgId Reclaim Group ID
 * @param rugId Free RU ID
 * 
 * @return void
*/
void putFreeRU(RGID_T rgId, RUGID_T rugId)
{
    ReclaimGroup *rg = &endgrp->rgs[rgId];
    ReclaimUnit *ru = &rg->rus[rugId];
    if(rg->free_ru.head == RU_NONE)
    {
        ru->prev_ru = RU_NONE;
        ru->next_ru = RU_NONE;
        rg->free_ru.head = rugId;
        rg->free_ru.tail = rugId;
    }    
    else
    {
        ru->prev_ru = rg->free_ru.tail;
        ru->next_ru = RU_NONE;
        rg->rus[rg->free_ru.tail].next_ru = rugId;
        rg->free_ru.tail = rugId;
    }
    rg->free_ru_cnt++;
}

/**
 * @brief Put a victim RU
 * 
 * @param rgId Reclaim Group ID
 * @param rugId Victim RU ID
 * @param ruhId RU Handle ID
 * @param invalidSliceCnt Invalid Slice Count
 * 
 * @return void
 * 
 * @details
 * This function is used to put a victim RU into the victim RU queue.
*/
void putVictimRU(RGID_T rgId, RUGID_T rugId, RUHID_T ruhId, SLICEID_T invalidSliceCnt)
{
    Queue16 *victimQ = &endgrp->rgs[rgId].victim_ru[ruhId][invalidSliceCnt];
    ReclaimUnit *ru = &endgrp->rgs[rgId].rus[rugId];
    if(victimQ->tail == RU_NONE)
    {
        victimQ->head = rugId;
        victimQ->tail = rugId;
        ru->prev_ru = RU_NONE;
        ru->next_ru = RU_NONE;
    }
    else
    {
        ru->prev_ru = victimQ->tail;
        ru->next_ru = RU_NONE;
        endgrp->rgs[rgId].rus[victimQ->tail].next_ru = rugId;
        victimQ->tail = rugId;
    }
}


/**
 * @brief Get a victim RU
 * 
 * @param rgId Reclaim Group ID
 * @param ruhId RU Handle ID
 * 
 * @return Victim RU ID
 * 
 * @details
 * This function is used to get a victim RU for GC.
 * Search the victim RU in the victim RU queue with the RU Handle ID.
 * Greedily select the RU with the most invalid slices.
 * 
 * Note: we can take the RUHT into consideration,
 * if ruht == RUHT_INITIALLY_ISOLATED, it means we can also search RU with other RUH,
 * but if the RU is currently used by another RUH, we should not select it.
*/
RUGID_T getVictimRU(RGID_T rgId, RUHID_T ruhId)
{
    RUGID_T rugId = RU_NONE;
    Queue16 *victimQ;
    for(SLICEID_T invalidSliceCnt = FDP_C_SLICE_PER_RU; invalidSliceCnt >= 0; invalidSliceCnt--)
    {
        victimQ = &endgrp->rgs[rgId].victim_ru[ruhId][invalidSliceCnt];
        if(victimQ->head == RU_NONE)
            continue;
        rugId = victimQ->head;
        if(victimQ->head == victimQ->tail)
        {
            victimQ->head = RU_NONE;
            victimQ->tail = RU_NONE;
        }
        else
        {
            victimQ->head = endgrp->rgs[rgId].rus[rugId].next_ru;
            endgrp->rgs[rgId].rus[victimQ->head].prev_ru = RU_NONE;
        }
        return rugId;
    }

    // Check RUHT
    if(endgrp->fdp.ruhs[ruhId].ruht == RUHT_INITIALLY_ISOLATED)
    {
        // Search in other RUH
        for(RUHID_T i = 0; i < FDP_CONF_NRUH; i++)
        {
            if(i == ruhId)
                continue;
            for(SLICEID_T invalidSliceCnt = FDP_C_SLICE_PER_RU; invalidSliceCnt >= 0; invalidSliceCnt--)
            {
                victimQ = &endgrp->rgs[rgId].victim_ru[i][invalidSliceCnt];
                if(victimQ->head == RU_NONE)
                    continue;
                rugId = victimQ->head;

                // Check if the RU is currently used by another RUH
                if(rugId == endgrp->fdp.ruhs[ruhId].rus[rgId])
                {
                    rugId = endgrp->rgs[rgId].rus[rugId].next_ru;
                    if(rugId == RU_NONE)
                        continue;
                }

                if(victimQ->head == victimQ->tail)
                {
                    victimQ->head = RU_NONE;
                    victimQ->tail = RU_NONE;
                }
                else
                {
                    victimQ->head = endgrp->rgs[rgId].rus[rugId].next_ru;
                    endgrp->rgs[rgId].rus[victimQ->head].prev_ru = RU_NONE;
                }
                return rugId;
            }
        }
    }

    assert(!"[WARNING] There are no free blocks. Abort terminate this ssd. [WARNING]");
    return rugId;
}

/**
 * @brief Pop a victim RU
 * 
 * @param rgId Reclaim Group ID
 * @param rugId Victim RU ID
 * 
 * @return void
 * 
 * @details
 * This function is used to unlink a victim RU from the victim RU queue.
 * It is used when we want to move the RU to another queue.
*/
void popVictimRU(RGID_T rgId, RUGID_T rugId)
{
    SLICEID_T invalidSliceCnt = endgrp->rgs[rgId].rus[rugId].invalid_slices;
    RUHID_T ruhId = endgrp->rgs[rgId].rus[rugId].ruhid;
    Queue16 *victimQ = &endgrp->rgs[rgId].victim_ru[ruhId][invalidSliceCnt];
    ReclaimUnit *ru = &endgrp->rgs[rgId].rus[rugId];
    ReclaimUnit *tmp;
    if(ru->prev_ru != RU_NONE)
    {
        tmp = &endgrp->rgs[rgId].rus[ru->prev_ru];
        tmp->next_ru = ru->next_ru;
    }
    else
    {
        victimQ->head = ru->next_ru;
    }
    if(ru->next_ru != RU_NONE)
    {
        tmp = &endgrp->rgs[rgId].rus[ru->next_ru];
        tmp->prev_ru = ru->prev_ru;
    }
    else
    {
        victimQ->tail = ru->prev_ru;
    }
    ru->prev_ru = RU_NONE;
    ru->next_ru = RU_NONE;
}

