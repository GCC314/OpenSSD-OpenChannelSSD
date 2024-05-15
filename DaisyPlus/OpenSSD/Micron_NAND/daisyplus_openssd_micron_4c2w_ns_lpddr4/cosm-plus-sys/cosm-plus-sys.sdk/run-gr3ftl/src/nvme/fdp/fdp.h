//////////////////////////////////////////////////////////////////////////////////
// Engineer: Guoci Chen <gcc314@stu.pku.edu.cn>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Flexible Data Placement
// File Name: fdp.h
//
// Version: v1.0.0
//
// Description:
//	 - TODO
//////////////////////////////////////////////////////////////////////////////////

#ifndef FDP_H_
#define FDP_H_

#include "xil_types.h"
#include "../../memory_map.h"
#include "../../ftl_config.h"


//---------------------------------------
// FDP Constants & Macros
//---------------------------------------

#define FDP_MAX_EVENTS 63

#define RUHT_INITIALLY_ISOLATED             1
#define RUHT_PERSISTENTLY_ISOLATED          2

#define RUHID_NONE  0xff
#define RU_NONE     0xffff

#define RGID_T              uint16_t
#define RUHID_T             uint16_t
#define RUGID_T             uint16_t
#define RUADDR_T            uint16_t
#define BLOADDR_T           uint32_t
#define SLICEID_T           int16_t

//---------------------------------------
// Miscellaneous Functions & Structures
//---------------------------------------

typedef struct PHINFO_PSTH {
    union {
        uint32_t data;
        struct {
            RGID_T rgid;
            RUHID_T ruhid;
        };
    };
} PHINFO_PSTH;                  // Placement Hint Info for passthrough the request layer
                                // used at req.nandInfo.programmedPageCnt

typedef struct Queue16 {
    uint16_t head, tail;
} Queue16;

//---------------------------------------
// FDP Configuration
//
// Note: Configuration should be done before compiling the firmware,
// as NVMe namespace management is currently not supported.
//---------------------------------------

#define FDP_CONF_ENABLED            1
#define FDP_CONF_NRUH               4
#define FDP_CONF_NRG                2
#define FDP_CONF_RGIF               1       // i.e. ceil(log2(FDP_NRG))
#define FDP_CONF_RUSIZE_BLOCKS      2       // Reclaim Unit Size in Blocks
#define FDP_CONF_RUNS               (FDP_CONF_RUSIZE_BLOCKS * BYTES_PER_DATA_REGION_OF_SLICE * SLICES_PER_BLOCK)
#define FDP_CONF_RUHT               RUHT_INITIALLY_ISOLATED
#define FDP_CONF_NSCNT              USER_CHANNELS
// Note: FDP_RUNS in this impl should be a multiple of BLOCK size,
// as block is the smallest unit of Garbage Collection in this impl.

#define FDP_C_RUCNT                 (USER_BLOCKS_PER_SSD / FDP_CONF_RUSIZE_BLOCKS)
#define FDP_C_RUCNT_PER_GROUP       (FDP_C_RUCNT / FDP_CONF_NRG)
#define FDP_C_SLICE_PER_RU          (FDP_CONF_RUSIZE_BLOCKS * SLICES_PER_BLOCK)

//---------------------------------------
// Reclaim Unit Management
//---------------------------------------

typedef struct BlockRUInfo {
    RUADDR_T ru_addr;          // rugId << RGIF | rgId
} BlockRUInfo;                  // Reclaim Unit Info for a Block

typedef struct BlockRUInfoTable {
    BlockRUInfo blocks[USER_DIES][USER_BLOCKS_PER_DIE];
} BlockRUInfoTable;

BlockRUInfoTable *blockRUInfoTable;

//---------------------------------------
// Necessary NVMe structures
//---------------------------------------

typedef struct ReclaimUnit {
    uint64_t ruamw;             // Reclaim Unit Available Media Writes

    RUHID_T ruhid;              // RUH currently using this RU
    BLOADDR_T blo_addr[FDP_CONF_RUSIZE_BLOCKS];  // DieId << 11 | BlockId
    
    SLICEID_T current_slice;
    SLICEID_T invalid_slices;
    
    RUGID_T prev_ru;
    RUGID_T next_ru;
} ReclaimUnit;

typedef struct ReclaimGroup {
    ReclaimUnit rus[FDP_C_RUCNT_PER_GROUP];
    RUGID_T free_ru_cnt;
    Queue16 free_ru;
    Queue16 victim_ru[FDP_CONF_NRUH][FDP_C_SLICE_PER_RU + 1];
} ReclaimGroup;

typedef struct RUHandle {
    uint8_t  ruht;              // Reclaim Unit Handle Type
    uint8_t  ruha;              // Reclaim Unit Handle Attributes
    uint64_t event_filter;      
    uint8_t  lbafi;             // Logical Block Address Format Identifier
    uint64_t ruamw;             // Reclaim Unit Available Media Writes

    RUGID_T rus[FDP_CONF_NRG]; // Reclaim Units
} RUHandle;

typedef struct FDPEvent {
    uint8_t  type;
    uint8_t  flags;
    uint16_t pid;
    uint64_t timestamp;
    uint32_t nsid;
    uint64_t type_specific[2];
    uint16_t rgid;
    uint8_t  ruhid;
    uint8_t  rsvd35[5];
    uint64_t vendor[3];
} FDPEvent;                     // TBD

typedef struct FDPEventBuffer {
    FDPEvent         events[FDP_MAX_EVENTS];
    unsigned int     nelems;
    unsigned int     start;
    unsigned int     next;
} FDPEventBuffer;               // TBD

typedef struct NamespaceFDP {
    struct {
        uint16_t nphs;          // Number of Placement Handles
        uint16_t *phs;          // Placement Handles, to index RUHs
    } fdp;
} NamespaceFDP;                 // Named with suffix FDP to avoid conflict with future definitions

typedef struct EnduranceGroup {
    uint8_t event_conf;

    struct {
        FDPEventBuffer host_events, ctrl_events;

        uint16_t nruh;          // Number of Reclaim Unit Handles
        uint16_t nrg;           // Number of Reclaim Groups
        uint8_t  rgif;          // Reclaim Group Identifier Format
        uint64_t runs;          // Reclaim Unit Nominal Size, in bytes

        uint64_t hbmw;          // Host Bytes with Metadata Written
        uint64_t mbmw;          // Media Bytes with Metadata Written
        uint64_t mbe;           // Media Bytes Erased

        int enabled;

        RUHandle *ruhs;         // Reclaim Unit Handles
    } fdp;

    NamespaceFDP *ns;           // Namespaces in the Endurance Group

    ReclaimGroup *rgs;          // Reclaim Groups
} EnduranceGroup;

extern EnduranceGroup *endgrp;

//---------------------------------------
// FDP Memory Map
//---------------------------------------

#define FDP_ENDGRP_ADDR             (FDP_MANAGEMENT_START_ADDR + 0x00000000)
#define FDP_RUHS_ADDR               (FDP_ENDGRP_ADDR + sizeof(EnduranceGroup))
#define FDP_NS_ADDR                 (FDP_RUHS_ADDR + FDP_CONF_NRUH * sizeof(RUHandle))
#define FDP_PHS_ADDR                (FDP_NS_ADDR + sizeof(NamespaceFDP) * FDP_CONF_NSCNT)
#define FDP_METADATA_START_ADDR     (FDP_MANAGEMENT_START_ADDR + 0x00100000)

#define FDP_BLOCKRUINFO_TABLE_ADDR  (FDP_METADATA_START_ADDR + 0x00000000)
#define FDP_RG_ADDR                 (FDP_BLOCKRUINFO_TABLE_ADDR + sizeof(BlockRUInfoTable))
#define FDP_RESERVED_ADDR           (FDP_RG_ADDR + sizeof(ReclaimGroup) * FDP_CONF_NRG)

//---------------------------------------
// FDP Functions
//---------------------------------------

void FDPInit();

//---------------------------------------
// Queue Functions
//---------------------------------------

#define GET_FREE_RU_FOR_USE 0
#define GET_FREE_RU_FOR_GC  1
#define RESERVED_FREE_RU_CNT 1

RUGID_T getFreeRU(RGID_T rgId, char getMode);
void putFreeRU(RGID_T rgId, RUGID_T rugId);

void putVictimRU(RGID_T rgId, RUGID_T rugId, RUHID_T ruhId, SLICEID_T invalidSliceCnt);
RUGID_T getVictimRU(RGID_T rgId, RUHID_T ruhId);
void popVictimRU(RGID_T rgId, RUGID_T rugId);

#endif /* FDP_H_ */