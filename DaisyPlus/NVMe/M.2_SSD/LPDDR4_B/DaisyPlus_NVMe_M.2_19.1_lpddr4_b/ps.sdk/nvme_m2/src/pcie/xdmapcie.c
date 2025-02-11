/*
 * xdmapcie.c
 *
 *  Created on: 2020. 1. 14.
 *      Author: ProDesk
 */

#include "sleep.h"
#include "xdmapcie.h"
#include "xdmapcie_common.h"
#include "xparameters.h"	/* Defines for XPAR constants */
#include "xil_printf.h"
#include "../nvme/nvme.h"

/************************** Constant Definitions ****************************/
/* Parameters for the waiting for link up routine */
#define XDMAPCIE_LINK_WAIT_MAX_RETRIES 		10
#define XDMAPCIE_LINK_WAIT_USLEEP_MIN 		90000

/*
 * Command register offsets
 */
#define PCIE_CFG_CMD_IO_EN	0x00000001 /* I/O access enable */
#define PCIE_CFG_CMD_MEM_EN	0x00000002 /* Memory access enable */
#define PCIE_CFG_CMD_BUSM_EN	0x00000004 /* Bus master enable */
#define PCIE_CFG_CMD_PARITY	0x00000040 /* parity errors response */
#define PCIE_CFG_CMD_SERR_EN	0x00000100 /* SERR report enable */

/*
 * PCIe Configuration registers offsets
 */

#define PCIE_CFG_ID_REG			0x0000 /* Vendor ID/Device ID offset */
#define PCIE_CFG_CMD_STATUS_REG		0x0001 /*
						* Command/Status Register
						* Offset
						*/
#define PCIE_CFG_PRI_SEC_BUS_REG	0x0006 /*
						* Primary/Sec.Bus Register
						* Offset
						*/
#define PCIE_CFG_CAH_LAT_HD_REG		0x0003 /*
						* Cache Line/Latency Timer/
						* Header Type/
						* BIST Register Offset
						*/
#define PCIE_CFG_BAR_0_REG		0x0004 /* PCIe Base Addr 0 */

#define PCIE_CFG_FUN_NOT_IMP_MASK	0xFFFF
#define PCIE_CFG_HEADER_TYPE_MASK	0x00EF0000
#define PCIE_CFG_MUL_FUN_DEV_MASK	0x00800000

#define PCIE_CFG_MAX_NUM_OF_BUS		256
#define PCIE_CFG_MAX_NUM_OF_DEV		1
#define PCIE_CFG_MAX_NUM_OF_FUN		8

#define PCIE_CFG_PRIM_SEC_BUS		0x00070100
#define PCIE_CFG_BAR_0_ADDR		0x00001111


/****************************************************************************/
/**
* This function initializes a XDMA PCIe IP built as a root complex
*
*
* @param	XdmaPciePtr is a pointer to an instance of XDmaPcie data
*		structure represents a root complex IP.
* @param 	DeviceId is XDMA PCIe IP unique ID
*
* @return
*		- XST_SUCCESS if successful.
*		- XST_FAILURE if unsuccessful.
*
* @note 	None.
*
*
******************************************************************************/
static int PcieInitRootComplex(XDmaPcie *XdmaPciePtr, u16 DeviceId)
{
	int Status;
	u32 HeaderData;
	u32 InterruptMask;
	u8  BusNumber;
	u8  DeviceNumber;
	u8  FunNumber;
	u8  PortNumber;

	XDmaPcie_Config *ConfigPtr;

	ConfigPtr = XDmaPcie_LookupConfig(DeviceId);

	Status = XDmaPcie_CfgInitialize(XdmaPciePtr, ConfigPtr,
						ConfigPtr->BaseAddress);

	if (Status != XST_SUCCESS) {
		xil_printf("Failed to initialize PCIe Root Complex"
							"IP Instance\r\n");
		return XST_FAILURE;
	}

	if(!XdmaPciePtr->Config.IncludeRootComplex) {
		xil_printf("Failed to initialize...XDMA PCIE is configured"
							" as endpoint\r\n");
		return XST_FAILURE;
	}


	/* See what interrupts are currently enabled */
	XDmaPcie_GetEnabledInterrupts(XdmaPciePtr, &InterruptMask);
	//xil_printf("Interrupts currently enabled are %8X\r\n", InterruptMask);

	/* Make sure all interrupts disabled. */
	XDmaPcie_DisableInterrupts(XdmaPciePtr, XDMAPCIE_IM_ENABLE_ALL_MASK);


	/* See what interrupts are currently pending */
	XDmaPcie_GetPendingInterrupts(XdmaPciePtr, &InterruptMask);
	//xil_printf("Interrupts currently pending are %8X\r\n", InterruptMask);

	/* Just if there is any pending interrupt then clear it.*/
	XDmaPcie_ClearPendingInterrupts(XdmaPciePtr,
						XDMAPCIE_ID_CLEAR_ALL_MASK);

	/*
	 * Read enabled interrupts and pending interrupts
	 * to verify the previous two operations and also
	 * to test those two API functions
	 */

	XDmaPcie_GetEnabledInterrupts(XdmaPciePtr, &InterruptMask);
	//xil_printf("Interrupts currently enabled are %8X\r\n", InterruptMask);

	XDmaPcie_GetPendingInterrupts(XdmaPciePtr, &InterruptMask);
	//xil_printf("Interrupts currently pending are %8X\r\n", InterruptMask);

	/* Make sure link is up. */
	int Retries;
	Status = FALSE;
	/* check if the link is up or not */
        for (Retries = 0; Retries < XDMAPCIE_LINK_WAIT_MAX_RETRIES; Retries++) {
		if (XDmaPcie_IsLinkUp(XdmaPciePtr)){
			Status = TRUE;
		}
                usleep(XDMAPCIE_LINK_WAIT_USLEEP_MIN);
	}
	if (Status != TRUE ) {
		xil_printf("Link is not up\r\n");
		return XST_FAILURE;
	}

	xil_printf("Link is up\r\n");

	/*
	 * Read back requester ID.
	 */
	XDmaPcie_GetRequesterId(XdmaPciePtr, &BusNumber,
				&DeviceNumber, &FunNumber, &PortNumber);

	xil_printf("Bus Number is %02X\r\n"
			"Device Number is %02X\r\n"
				"Function Number is %02X\r\n"
					"Port Number is %02X\r\n",
			BusNumber, DeviceNumber, FunNumber, PortNumber);


	/* Set up the PCIe header of this Root Complex */
	XDmaPcie_ReadLocalConfigSpace(XdmaPciePtr,
					PCIE_CFG_CMD_STATUS_REG, &HeaderData);

	HeaderData |= (PCIE_CFG_CMD_BUSM_EN | PCIE_CFG_CMD_MEM_EN |
				PCIE_CFG_CMD_IO_EN | PCIE_CFG_CMD_PARITY |
							PCIE_CFG_CMD_SERR_EN);

	XDmaPcie_WriteLocalConfigSpace(XdmaPciePtr,
					PCIE_CFG_CMD_STATUS_REG, HeaderData);

	/*
	 * Read back local config reg.
	 * to verify the write.
	 */

	XDmaPcie_ReadLocalConfigSpace(XdmaPciePtr,
					PCIE_CFG_CMD_STATUS_REG, &HeaderData);

	xil_printf("PCIe Local Config Space is %8X at register"
					" CommandStatus\r\n", HeaderData);

	/*
	 * Set up Bus number
	 */

	HeaderData = PCIE_CFG_PRIM_SEC_BUS;

	XDmaPcie_WriteLocalConfigSpace(XdmaPciePtr,
					PCIE_CFG_PRI_SEC_BUS_REG, HeaderData);

	/*
	 * Read back local config reg.
	 * to verify the write.
	 */
	XDmaPcie_ReadLocalConfigSpace(XdmaPciePtr,
					PCIE_CFG_PRI_SEC_BUS_REG, &HeaderData);

	xil_printf("PCIe Local Config Space is %8X at register "
					"Prim Sec. Bus\r\n", HeaderData);

	XDmaPcie_GetRootPortStatusCtrl(XdmaPciePtr, (u32 *)&Status);
	XDmaPcie_SetRootPortStatusCtrl(XdmaPciePtr, Status | XDMAPCIE_RPSC_BRIDGE_ENABLE_MASK);

	/* Now it is ready to function */

	xil_printf("Root Complex IP Instance has been successfully"
							" initialized\r\n");

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* Read 32-bit value from external PCIe Function's configuration space.
* External PCIe function is identified by its Requester ID (Bus#, Device#,
* Function#). Location is identified by its offset from the begginning of the
* configuration space.
*
* @param 	InstancePtr is the PCIe component to operate on.
* @param 	Bus is the external PCIe function's Bus number.
* @param 	Device is the external PCIe function's Device number.
* @param 	Function is the external PCIe function's Function number.
* @param 	Offset from beggininng of PCIe function's configuration space.
* @param 	DataPtr is a pointer to a variable where the driver will pass
* 		back the value read from the specified location.
*
* @return 	None
*
* @note 	This function is valid only when IP is configured as a
*		root complex. The XDmaPcie_ReadLocalConfigSpace API should
*		be used for reading the local config space.
*
*****************************************************************************/
static void XDmaPcie_ReadRemoteConfigSpaceLocal(XDmaPcie *InstancePtr, u8 Bus, u8 Device,
		 u8 Function, u16 Offset, u32 *DataPtr)
{
	u32 Location = 0;
	u32 Data;

	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(DataPtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(InstancePtr->Config.IncludeRootComplex ==
							XDMAPCIE_IS_RC);

	if (((Bus == 0) && !((Device == 0) && (Function == 0))) ||
		(Bus > InstancePtr->MaxNumOfBuses)) {
		*DataPtr = 0xFFFFFFFF;
		return;
	}

	/* Compose function configuration space location */
	Location = XDmaPcie_ComposeExternalConfigAddress (Bus, Device,
							Function, Offset);

	while(XDmaPcie_IsEcamBusy(InstancePtr));

	/* Read data from that location */
	Data = XDmaPcie_ReadReg((InstancePtr->Config.BaseAddress),
								Location);
	*DataPtr = Data;

}

/****************************************************************************/
/**
* Write 32-bit value to external PCIe function's configuration space.
* External PCIe function is identified by its Requester ID (Bus#, Device#,
* Function#). Location is identified by its offset from the begginning of the
* configuration space.
*
* @param 	InstancePtr is the PCIe component to operate on.
* @param 	Bus is the external PCIe function's Bus number.
* @param 	Device is the external PCIe function's Device number.
* @param 	Function is the external PCIe function's Function number.
* @param 	Offset from beggininng of PCIe function's configuration space.
* @param 	Data to be written to the specified location.
*
* @return 	None
*
* @note 	This function is valid only when IP is configured as a
*		root complex. The XDmaPcie_WriteLocalConfigSpace should be
*		used for writing to local config space.
*
*****************************************************************************/
static void XDmaPcie_WriteRemoteConfigSpaceLocal(XDmaPcie *InstancePtr, u8 Bus, u8 Device,
					 u8 Function, u16 Offset, u32 Data)
{
	u32 Location = 0;
	u32 TestWrite = 0;
	u8 Count = 3;

	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(InstancePtr->Config.IncludeRootComplex ==
							XDMAPCIE_IS_RC);

	if (Bus > InstancePtr->MaxNumOfBuses) {
		return;
	}

	/* Compose function configuration space location */
	Location = XDmaPcie_ComposeExternalConfigAddress (Bus, Device,
							Function, Offset);
	while(XDmaPcie_IsEcamBusy(InstancePtr));


	/* Write data to that location */
	XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress),
				Location , Data);


	/* Read data from that location to verify write */
	while (Count) {

		TestWrite =
			XDmaPcie_ReadReg((InstancePtr->Config.BaseAddress),
								Location);

		if (TestWrite == Data) {
			break;
		}

		Count--;
	}
}

/******************************************************************************/
/**
* This function reserves bar memory address.
*
* @param   InstancePtr pointer to XDmaPcie Instance Pointer
* @param   mem_type type of bar memory. address mem or IO.
* @param   mem_as bar memory tpye 32 or 64 bit
* @param   size	u64 size to increase
*
* @return  bar address
*
*******************************************************************************/
static u64 XDmaPcie_ReserveBarMem(XDmaPcie *InstancePtr, u8 MemType,
		u8 MemBarArdSize, u32 Size_Org, u64 Size)
{
	u64 Ret = 0;

	if (MemType == XDMAPCIE_BAR_IO_MEM){
		Ret = XST_FAILURE;
		goto End;
	}

	if (Size_Org & 0x08) {
		Ret = InstancePtr->Config.PMemBaseAddr + (Size_Org & 0x0F);
		InstancePtr->Config.PMemBaseAddr = InstancePtr->Config.PMemBaseAddr
							+ Size;
		Xil_AssertNonvoid(InstancePtr->Config.PMemBaseAddr <=
				InstancePtr->Config.PMemMaxAddr);
	} else {
		Ret = InstancePtr->Config.NpMemBaseAddr + (Size_Org & 0x0F);
		InstancePtr->Config.NpMemBaseAddr = InstancePtr->Config.NpMemBaseAddr
							+ Size;
		Xil_AssertNonvoid(InstancePtr->Config.NpMemBaseAddr <=
				InstancePtr->Config.NpMemMaxAddr);
	}

End:
	return Ret;
}

static int XDmaPcie_PositionRightmostSetbit(u64 Size)
{
	int Position = 0;
	int Bit = 1;

	/* ignore 4 bits */
	Size = Size & (~(0xf));

	while (!(Size & Bit)) {
		Bit = Bit << 1;
		Position++;
	}

	return Position;
}
/******************************************************************************/
/**
* This function increments to next 1Mb page starting position of
* non prefetchable memory
*
* @param   	InstancePtr pointer to XDmaPcie Instance Pointer
*
*******************************************************************************/
static void XDmaPcie_IncreamentNpMem(XDmaPcie *InstancePtr)
{
	InstancePtr->Config.NpMemBaseAddr >>= MB_SHIFT;
	InstancePtr->Config.NpMemBaseAddr++;
	InstancePtr->Config.NpMemBaseAddr <<= MB_SHIFT;
}

/******************************************************************************/
/**
* This function Composes configuration space location
*
* @param   InstancePtr pointer to XDmaPcie Instance Pointer
* @param   headerType u32 type0 or type1 header
* @param   Bus
* @param   Device
* @param   Function
*
* @return  int XST_SUCCESS on success
*          err on fail
*
*******************************************************************************/
static int XDmaPcie_AllocBarSpace(XDmaPcie *InstancePtr, u32 Headertype, u8 Bus,
                           u8 Device, u8 Function)
{
	u32 Data = DATA_MASK_32;
	u32 Location = 0, Location_1 = 0;
	u32 Size = 0, Size_1 = 0, TestWrite;
	u8 MemAs, MemType;
	u64 BarAddr;
	u32 Tmp, *PPtr;
	u8 BarNo;

	u8 MaxBars = 0;

	if (Headertype == XDMAPCIE_CFG_HEADER_O_TYPE) {
		/* For endpoints */
		MaxBars = 6;
	} else {
		/* For Bridge*/
		MaxBars = 2;
	}

	for (BarNo = 0; BarNo < MaxBars; BarNo++) {
		/* Compose function configuration space location */
		Location = XDmaPcie_ComposeExternalConfigAddress(
			Bus, Device, Function,
			XDMAPCIE_CFG_BAR_BASE_OFFSET + BarNo);

		/* Write data to that location */
		XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress), Location, Data);

		Size = XDmaPcie_ReadReg((InstancePtr->Config.BaseAddress), Location);
		if ((Size & (~(0xf))) == 0x00) {
			/* return saying that BAR is not implemented */
			XDmaPcie_Dbg(
				"bus: %d, device: %d, function: %d: BAR %d is "
				"not implemented\r\n",
				Bus, Device, Function, BarNo);
			XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress), Location, 0x00);
			continue;
		}

		/* check for IO space or memory space */
		if (Size & XDMAPCIE_CFG_BAR_MEM_TYPE_MASK) {
			/* Device required IO address space */
			MemType = XDMAPCIE_BAR_IO_MEM;
			XDmaPcie_Dbg(
				"bus: %d, device: %d, function: %d: BAR %d "
				"required IO space; it is unassigned\r\n",
				Bus, Device, Function, BarNo);
			continue;
		} else {
			/* Device required memory address space */
			MemType = XDMAPCIE_BAR_ADDR_MEM;
		}

		/* check for 32 bit AS or 64 bit AS */
		if ((Size & 0x6) == 0x4) {
			/* 64 bit AS is required */
			MemAs = XDMAPCIE_BAR_MEM_TYPE_64;

			/* Compose function configuration space location */
			Location_1 = XDmaPcie_ComposeExternalConfigAddress(
				Bus, Device, Function,
				XDMAPCIE_CFG_BAR_BASE_OFFSET + (BarNo + 1));

			/* Write data to that location */
			XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress),
						Location_1, Data);

			/* get next bar if 64 bit address is required */
			Size_1 = XDmaPcie_ReadReg((InstancePtr->Config.BaseAddress),
						Location_1);

			/* Merge two bars for size */
			PPtr = (u32 *)&BarAddr;
			*PPtr = Size;
			*(PPtr + 1) = Size_1;

			TestWrite = XDmaPcie_PositionRightmostSetbit(BarAddr);

			/* actual bar size is 2 << TestWrite */
			BarAddr =
				XDmaPcie_ReserveBarMem(InstancePtr, MemType, MemAs, Size,
						(2 << (TestWrite - 1)));

			Tmp = (u32)BarAddr;

			/* Write actual bar address here */
			XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress), Location,
					  Tmp);

			Tmp = (u32)(BarAddr >> 32);

			/* Write actual bar address here */
			XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress),
						Location_1, Tmp);
			XDmaPcie_Dbg(
				"bus: %d, device: %d, function: %d: BAR %d, "
				"ADDR: 0x%p size : %dK\r\n",
				Bus, Device, Function, BarNo, BarAddr,
				((2 << (TestWrite - 1)) / 1024));
		} else {
			/* 32 bit AS is required */
			MemAs = XDMAPCIE_BAR_MEM_TYPE_32;

			TestWrite = XDmaPcie_PositionRightmostSetbit(Size);

			/* actual bar size is 2 << TestWrite */
			BarAddr =
				XDmaPcie_ReserveBarMem(InstancePtr, MemType, MemAs, Size,
						(2 << (TestWrite - 1)));

			Tmp = (u32)BarAddr;

			/* Write actual bar address here */
			XDmaPcie_WriteReg((InstancePtr->Config.BaseAddress), Location,
					Tmp);
			XDmaPcie_Dbg(
				"bus: %d, device: %d, function: %d: BAR %d, "
				"ADDR: 0x%p size : %dK\r\n",
				Bus, Device, Function, BarNo, BarAddr,
				((2 << (TestWrite - 1)) / 1024));
		}
		/* no need to probe next bar if present BAR requires 64 bit AS
		 */
		if ((Size & 0x6) == 0x4)
			BarNo = BarNo + 1;
	}

	return XST_SUCCESS;
}

/******************************************************************************/
/**
* This function increments to next 1Mb block starting position of
* prefetchable memory
*
* @param  	InstancePtr pointer to XDmaPcie Instance
*
*******************************************************************************/
static void XDmaPcie_IncreamentPMem(XDmaPcie *InstancePtr)
{
	InstancePtr->Config.PMemBaseAddr >>= MB_SHIFT;
	InstancePtr->Config.PMemBaseAddr++;
	InstancePtr->Config.PMemBaseAddr <<= MB_SHIFT;
}

/******************************************************************************/
/**
* This function starts enumeration of PCIe Fabric on the system.
* Assigns primary, secondary and subordinate bus numbers.
* Assigns memory to prefetchable and non-prefetchable memory locations.
* enables end-points and bridges.
*
* @param   	InstancePtr pointer to XDmaPcie Instance Pointer
* @param   	bus_num	to scans for connected bridges/endpoints on it.
*
* @return  	none
*
*******************************************************************************/
static void XDmaPcie_FetchDevicesInBus(XDmaPcie *InstancePtr, u32 BusNum, u32 InitLastBusNum)
{
	u32 ConfigData;
	static u32 LastBusNum;

	u16 PCIeVendorID;
	u16 PCIeDeviceID;
	u32 PCIeHeaderType;
	u32 PCIeMultiFun;

	u32 Adr06; /* Latency timer */
	u32 Adr08;
	u32 Adr09;
	u32 Adr0A;
	u32 Adr0B;

	int Ret;

    if(InitLastBusNum)
    {
        LastBusNum = 0;
    }
    
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	if (BusNum > InstancePtr->MaxNumOfBuses) {
		/* End of bus size */
		return;
	}

	for (u32 PCIeDevNum = 0; PCIeDevNum < XDMAPCIE_CFG_MAX_NUM_OF_DEV;
	     PCIeDevNum++) {
		for (u32 PCIeFunNum = 0; PCIeFunNum < XDMAPCIE_CFG_MAX_NUM_OF_FUN;
		     PCIeFunNum++) {

			/* Vendor ID */
			XDmaPcie_ReadRemoteConfigSpaceLocal(
				InstancePtr, BusNum, PCIeDevNum, PCIeFunNum,
				XDMAPCIE_CFG_ID_REG, &ConfigData);

			PCIeVendorID = (u16)(ConfigData & 0xFFFF);
			PCIeDeviceID = (u16)((ConfigData >> 16) & 0xFFFF);

			if (PCIeVendorID == XDMAPCIE_CFG_FUN_NOT_IMP_MASK) {
				if (PCIeFunNum == 0)
					/*
					 * We don't need to look
					 * any further on this device.
					 */
					break;
			} else {
				XDmaPcie_Dbg(
					"\n\rPCIeBus is %02X\r\nPCIeDev is "
					"%02X\r\nPCIeFunc is %02X\r\n",
					BusNum, PCIeDevNum, PCIeFunNum);

				XDmaPcie_Dbg(
					"Vendor ID is %04X \r\nDevice ID is "
					"%04X\r\n",
					PCIeVendorID, PCIeDeviceID);

				/* Header Type */
				XDmaPcie_ReadRemoteConfigSpaceLocal(
					InstancePtr, BusNum, PCIeDevNum,
					PCIeFunNum, XDMAPCIE_CFG_CAH_LAT_HD_REG,
					&ConfigData);

				PCIeHeaderType =
					ConfigData & XDMAPCIE_CFG_HEADER_TYPE_MASK;
				PCIeMultiFun =
					ConfigData & XDMAPCIE_CFG_MUL_FUN_DEV_MASK;

				if (PCIeHeaderType == XDMAPCIE_CFG_HEADER_O_TYPE) {
					/* This is an End Point */
					XDmaPcie_Dbg("This is an End Point\r\n");

					/*
					 * Write Address to PCIe BAR
					 */
					Ret = XDmaPcie_AllocBarSpace(
						InstancePtr, PCIeHeaderType,
						BusNum, PCIeDevNum,
						PCIeFunNum);
					if (Ret != 0)
						return;

					/*
					 * Initialize this end point
					 * and return.
					 */
					XDmaPcie_ReadRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_CMD_STATUS_REG,
						&ConfigData);

					ConfigData |= (XDMAPCIE_CFG_CMD_BUSM_EN
						       | XDMAPCIE_CFG_CMD_MEM_EN);

					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_CMD_STATUS_REG,
						ConfigData);

					XDmaPcie_Dbg(
						"End Point has been "
						"enabled\r\n");

					XDmaPcie_IncreamentNpMem(InstancePtr);
					XDmaPcie_IncreamentPMem(InstancePtr);

				} else {
					/* This is a bridge */
					XDmaPcie_Dbg("This is a Bridge\r\n");

					/* alloc bar space and configure bridge
					 */
					Ret = XDmaPcie_AllocBarSpace(
						InstancePtr, PCIeHeaderType,
						BusNum, PCIeDevNum,
						PCIeFunNum);

					if (Ret != 0)
						continue;

					Adr06 = 0x0; /* Latency timer */
					Adr08 = 0x0;
					Adr09 = 0x0;
					Adr0A = 0x0;
					Adr0B = 0x0;

					/* Sets primary and secondary bus
					 * numbers */
					Adr06 <<= TWO_HEX_NIBBLES;
					Adr06 |= 0xFF; /* sub ordinate bus no 0xF
						     */
					Adr06 <<= TWO_HEX_NIBBLES;
					Adr06 |= (++LastBusNum); /* secondary
							      bus no */
					Adr06 <<= TWO_HEX_NIBBLES;
					Adr06 |= BusNum; /* Primary bus no */
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_BUS_NUMS_T1_REG,
						Adr06);

					/* Update start values of P and NP MMIO
					 * base */
					Adr08 |= ((InstancePtr->Config.NpMemBaseAddr
						   & 0xFFF00000)
						  >> FOUR_HEX_NIBBLES);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_NP_MEM_T1_REG, Adr08);

					Adr09 |= ((InstancePtr->Config.PMemBaseAddr
						   & 0xFFF00000)
						  >> FOUR_HEX_NIBBLES);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_P_MEM_T1_REG, Adr09);
					Adr0A |= (InstancePtr->Config.PMemBaseAddr
						  >> EIGHT_HEX_NIBBLES);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_P_UPPER_MEM_T1_REG,
						Adr0A);

					/* Searches secondary bus devices. */
					XDmaPcie_FetchDevicesInBus(InstancePtr,
							  LastBusNum, FALSE);

					/*
					 * update subordinate bus no
					 * clearing subordinate bus no
					 */
					Adr06 &= (~(0xFF << FOUR_HEX_NIBBLES));
					/* setting subordinate bus no */
					Adr06 |= (LastBusNum
						  << FOUR_HEX_NIBBLES);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_BUS_NUMS_T1_REG,
						Adr06);

					/*
					 * Update end values of MMIO limit
					 */

					/*
					 * Align memory to 1 Mb boundry.
					 *
					 * eg. 0xE000 0000 is the base address. Increments
					 * 1 Mb which gives 0xE010 0000 and writes to limit.
					 * So the final value at DW08(in pcie type 1 header)
					 * is 0xE010 E000.
					 * So the range is 0xE000 0000 to 0xE01F FFFF.
					 *
					 */
					XDmaPcie_IncreamentNpMem(InstancePtr);
					Adr08 |= (InstancePtr->Config.NpMemBaseAddr
						  & 0xFFF00000);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_NP_MEM_T1_REG, Adr08);

					XDmaPcie_IncreamentPMem(InstancePtr);
					Adr09 |= (InstancePtr->Config.PMemBaseAddr
						  & 0xFFF00000);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_P_MEM_T1_REG, Adr09);
					Adr0B |= (InstancePtr->Config.PMemBaseAddr
						  >> EIGHT_HEX_NIBBLES);
					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_P_LIMIT_MEM_T1_REG,
						Adr0B);

					/* Increment P & NP mem to next aligned starting address.
					 *
					 * Eg: As the range is 0xE000 0000 to 0xE01F FFFF.
					 * the next starting address should be 0xE020 0000.
					 */
					XDmaPcie_IncreamentNpMem(InstancePtr);
					XDmaPcie_IncreamentPMem(InstancePtr);

					/*
					 * Enable configuration
					 */
					XDmaPcie_ReadRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_CMD_STATUS_REG,
						&ConfigData);

					ConfigData |= (XDMAPCIE_CFG_CMD_BUSM_EN
						       | XDMAPCIE_CFG_CMD_MEM_EN);

					XDmaPcie_WriteRemoteConfigSpaceLocal(
						InstancePtr, BusNum,
						PCIeDevNum, PCIeFunNum,
						XDMAPCIE_CFG_CMD_STATUS_REG,
						ConfigData);
				}
			}
			if ((!PCIeFunNum) && (!PCIeMultiFun)) {
				/*
				 * If it is function 0 and it is not a
				 * multi function device, we don't need
				 * to look any further on this devie
				 */
				break;
			}
		}
	}
}

/******************************************************************************/
/**
* This function starts PCIe enumeration.
*
* @param    InstancePtr pointer to XDmaPcie Instance Pointer
*
* @return 	none
*
*******************************************************************************/
static void XDmaPcie_EnumeratePCIe(XDmaPcie *InstancePtr)
{
	XDmaPcie_FetchDevicesInBus(InstancePtr, 0, TRUE);
}

static u16 DeviceId[NUM_NVME_M2] = {XPAR_XDMAPCIE_0_DEVICE_ID, XPAR_XDMAPCIE_1_DEVICE_ID};
static XDmaPcie XdmaPcieInstance[NUM_NVME_M2];

int pcie_init(u64 *bar)
{
	int Status = XST_SUCCESS;
	int i;

    for(i = 0; i < NUM_NVME_M2; i++)
    {
    	/* Initialize Root Complex */
	    Status = PcieInitRootComplex(&XdmaPcieInstance[i], DeviceId[i]);
    	if (Status != XST_SUCCESS) {
	    	xil_printf("PcieInitRootComplex() Failed\r\n");
    		return XST_FAILURE;
	    }

	    bar[i] = XdmaPcieInstance[i].Config.NpMemBaseAddr;

    	/* Scan PCIe Fabric */
    	XDmaPcie_EnumeratePCIe(&XdmaPcieInstance[i]);
    }
    
	return Status;
}
