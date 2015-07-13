#include <cstdio>
#include <string>
#include <algorithm>
#include <stdint.h>
#include <bitset>
#include "utils/file.h"
#include "utils/string.h"
#include <SDL.h>
#undef main

/*
KUSEG     KSEG0     KSEG1
00000000h 80000000h A0000000h  2048K  Main RAM(first 64K reserved for BIOS)
1F000000h 9F000000h BF000000h  8192K  Expansion Region 1 (ROM / RAM)
1F800000h 9F800000h    --      1K     Scratchpad(D - Cache used as Fast RAM)
1F801000h 9F801000h BF801000h  8K     I / O Ports
1F802000h 9F802000h BF802000h  8K     Expansion Region 2 (I / O Ports)
1FA00000h 9FA00000h BFA00000h  2048K  Expansion Region 3 (whatever purpose)
1FC00000h 9FC00000h BFC00000h  512K   BIOS ROM(Kernel) (4096K max)
       FFFE0000h(KSEG2)        0.5K   I / O Ports(Cache Control)
*/

uint8_t bios[512 * 1024]; //std::vector<uint8_t> bios;
uint8_t ram[2 * 1024 * 1024]; //std::vector<uint8_t> ram;
uint8_t scratchpad[1024]; //std::vector<uint8_t> scratchpad;
uint8_t io[8 * 1024]; //std::vector<uint8_t> io;
uint8_t expansion[0x10000]; //std::vector<uint8_t> expansion;

std::string regNames[] = {
	"r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
	"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
	"t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

struct CPU
{
	uint32_t PC;
	uint32_t jumpPC; 
	bool shouldJump;
	uint32_t reg[32];
	uint32_t COP0[32];
	uint32_t hi, lo;

	CPU()
	{
		PC = 0xBFC00000;
		jumpPC = 0;
		shouldJump = false;
		for (int i = 0; i < 32; i++) reg[i] = 0;
		for (int i = 0; i < 32; i++) COP0[i] = 0;
		hi = 0;
		lo = 0;
	}
};

bool IsC = false;
uint8_t POST = 0;
bool disassemblyEnabled = false;
bool memoryAccessLogging = false;

std::string part = "";

uint8_t readMemory(uint32_t address)
{
	uint32_t _address = address;
	uint32_t data = 0;
	if (address >= 0xa0000000) address -= 0xa0000000;
	if (address >= 0x80000000) address -= 0x80000000;

	part = "";

	// RAM
	if (address >= 0x00000000 &&
		address <=   0x1fffff)
	{
		data = ram[address];
		//printf("RAM_R: 0x%02x (0x%08x) - 0x%02x\n", address, _address, data);
	}

	// Expansion port (mirrored?)
	else if (address >= 0x1f000000 &&
		address <= 0x1f00ffff)
	{
		address -= 0x1f000000;
		data = expansion[address];
		part = "EXPANSION";
	}

	// Scratch Pad
	else if (address >= 0x1f800000 &&
			address <= 0x1f8003ff)
	{
		address -= 0x1f800000;
		data = scratchpad[address];
		//part = "  SCRATCH";
	}

	// IO Ports
	else if (address >= 0x1f801000 &&
			address <= 0x1f803000)
	{
		address -= 0x1f801000;
		data = io[address];
		if (address >= 0x0000 && address <= 0x0024) part = " MEMCTRL1";
		else if (address >= 0x0040 && address <= 0x005F) part = "   PERIPH";
		else if (address >= 0x0060 && address <= 0x0063) part = " MEMCTRL2";
		else if (address >= 0x0070 && address <= 0x0077) part = "  INTCTRL";
		else if (address >= 0x0080 && address <= 0x00FF) part = "      DMA";
		else if (address >= 0x0100 && address <= 0x012F) part = "    TIMER";
		else if (address >= 0x0800 && address <= 0x0803) part = "    CDROM";
		else if (address >= 0x0C00 && address <= 0x1FFF) part = "      SPU";
		else part = "       IO";
	}


	else if (address >= 0x1fc00000 && 
			address <= 0x1fc00000 + 512*1024)
	{
		address -= 0x1fc00000;
		
		data = bios[address];
	}

	else if (_address >= 0xfffe0000 &&
		_address <= 0xfffe0200)
	{
		address = _address - 0xfffe0000;
		data = 0;

		//part = "    CACHE";
	}

	else
	{
		printf("READ: Access to unmapped address: 0x%08x (0x%08x)\n", address, _address);
		data = 0;
	}
	return data;
}

// Word - 32bit
// Half - 16bit
// Byte - 8bit

uint8_t readMemory8(uint32_t address)
{
	// TODO: Check address align
	uint8_t data = readMemory(address);
	if (!part.empty() && memoryAccessLogging) printf("%s_R08: 0x%08x - 0x%02x '%c'\n", part.c_str(), address, data, data);
	return data;
}

uint16_t readMemory16(uint32_t address)
{
	// TODO: Check address align
	uint16_t data = 0;
	data |= readMemory(address + 0);
	data |= readMemory(address + 1) << 8;
	if (!part.empty() && memoryAccessLogging) printf("%s_R16: 0x%08x - 0x%04x\n", part.c_str(), address, data);
	return data;
}

uint32_t readMemory32(uint32_t address)
{
	// TODO: Check address align
	uint32_t data = 0;
	data |= readMemory(address + 0);
	data |= readMemory(address + 1) << 8;
	data |= readMemory(address + 2) << 16;
	data |= readMemory(address + 3) << 24;
	if (!part.empty() && memoryAccessLogging) printf("%s_R32: 0x%08x - 0x%08x\n", part.c_str(), address, data);
	return data;
}

void writeMemory( uint32_t address, uint8_t data )
{
	uint32_t _address = address;
	if (address >= 0xa0000000) address -= 0xa0000000;
	if (address >= 0x80000000) address -= 0x80000000;

	part = "";

	// RAM
	if (address >= 0x00000000 &&
		address <= 0x1fffff)
	{
		if (!IsC) {
			ram[address] = data;
			if (disassemblyEnabled) part = "      RAM";
		}
		//printf("RAM_W: 0x%02x (0x%08x) - 0x%02x\n", address, _address, data);
	}

	// Expansion port (mirrored?)
	else if (address >= 0x1f000000 &&
		address <= 0x1f00ffff)
	{
		address -= 0x1f000000;
		expansion[address] = data;
		part = "EXPANSION";
	}

	// Scratch Pad
	else if (address >= 0x1f800000 &&
		address <= 0x1f8003ff)
	{
		address -= 0x1f800000;
		scratchpad[address] = data;
		//part = "  SCRATCH";
	}

	// IO Ports
	else if (address >= 0x1f801000 &&
			address <= 0x1f803000)
	{
		address -= 0x1f801000;

		if (address >= 0x0000 && address <= 0x0024) part = " MEMCTRL1";
		else if (address >= 0x0040 && address <= 0x005F) part = "   PERIPH";
		else if (address >= 0x0060 && address <= 0x0063) part = " MEMCTRL2";
		else if (address >= 0x0070 && address <= 0x0077) part = "  INTCTRL";
		else if (address >= 0x0080 && address <= 0x00FF) part = "      DMA";
		else if (address >= 0x0100 && address <= 0x012F) part = "    TIMER";
		else if (address >= 0x0800 && address <= 0x0803) part = "    CDROM";
		else if (address >= 0x0C00 && address <= 0x1FFF) part = "      SPU";
		else if (address == 0x1041) part = "     POST";
	}

	else if (address >= 0x1fc00000 &&
		address <= 0x1fc00000 + 512*1024)
	{
		address -= 0x1fc00000;

		printf("Write to readonly address (BIOS): 0x%08x\n", address);
	}

	else if (_address >= 0xfffe0000 &&
			_address <= 0xfffe0200)
	{
		address = _address - 0xfffe0000;
		//part = "    CACHE";
	}

	else
	{
		printf("WRITE: Access to unmapped address: 0x%08x (0x%08x)\n", address, _address);
		data = 0;
	}
}

void writeMemory8(uint32_t address, uint8_t data)
{
	// TODO: Check address align
	writeMemory(address, data);
	if (!part.empty() && memoryAccessLogging) printf("%s_W08: 0x%08x - 0x%02x '%c'\n", part.c_str(), address, data, data);
}

void writeMemory16(uint32_t address, uint16_t data)
{
	// TODO: Check address align
	writeMemory(address + 0, data & 0xff);
	writeMemory(address + 1, data >> 8);
	if (!part.empty() && memoryAccessLogging) printf("%s_W16: 0x%08x - 0x%04x\n", part.c_str(), address, data);
}

void writeMemory32(uint32_t address, uint32_t data)
{
	// TODO: Check address align
	writeMemory(address + 0, data);
	writeMemory(address + 1, data >> 8);
	writeMemory(address + 2, data >> 16);
	writeMemory(address + 3, data >> 24);
	if (!part.empty() && memoryAccessLogging) printf("%s_W32: 0x%08x - 0x%08x\n", part.c_str(), address, data);
}

union Opcode
{
	// I-Type: Immediate
	//    6bit   5bit   5bit          16bit
	//  |  OP  |  rs  |  rt  |        imm         |

	// J-Type: Jump
	//    6bit                26bit
	//  |  OP  |             target               |

	// R-Type: Register
	//    6bit   5bit   5bit   5bit   5bit   6bit
	//  |  OP  |  rs  |  rt  |  rd  |  sh  | fun  |

	// OP - 6bit operation code
	// rs, rt, rd - 5bit source, target, destination register
	// imm - 16bit immediate value
	// target - 26bit jump address
	// sh - 5bit shift amount
	// fun - 6bit function field

	struct
	{
		uint32_t fun : 6;
		uint32_t sh : 5;
		uint32_t rd : 5;
		uint32_t rt : 5;
		uint32_t rs : 5;
		uint32_t op : 6;
	};
	uint32_t opcode; // Whole 32bit opcode
	uint32_t target : 26; // JType instruction jump address
	uint16_t imm;    // IType immediate 
	int16_t offset;  // IType signed immediate (relative address)
};

std::string _mnemonic = "";
std::string _disasm = "";
std::string _pseudo = "";
bool executeInstruction(CPU *cpu, Opcode i)
{
	_pseudo = "";
	uint32_t addr = 0;

	bool isJumpCycle = true;
	
	#define mnemonic(x) if (disassemblyEnabled) _mnemonic = x
	#define disasm(fmt, ...) if (disassemblyEnabled) _disasm=string_format(fmt, ##__VA_ARGS__)
	#define pseudo(fmt, ...) if (disassemblyEnabled) _pseudo=string_format(fmt, ##__VA_ARGS__)

	// Jump
	// J target
	if (i.op == 2) {
		mnemonic("J");
		disasm("0x%x", (i.target << 2));
		cpu->shouldJump = true;
		isJumpCycle = false;
		cpu->jumpPC = (cpu->PC & 0xf0000000) | (i.target << 2);
	}

	// Jump And Link
	// JAL target
	else if (i.op == 3) {
		mnemonic("JAL");
		disasm("0x%x", (i.target << 2));
		cpu->shouldJump = true;
		isJumpCycle = false;
		cpu->jumpPC = (cpu->PC & 0xf0000000) | (i.target << 2);
		cpu->reg[31] = cpu->PC + 4;
	}

	// Branches

	// Branch On Equal
	// BEQ rs, rt, offset
	else if (i.op == 4) {
		mnemonic("BEQ");
		disasm("r%d, r%d, 0x%x", i.rs, i.rt, i.imm);

		if (cpu->reg[i.rt] == cpu->reg[i.rs]) {
			cpu->shouldJump = true;
			isJumpCycle = false;
			cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
		}
	}

	// Branch On Greater Than Zero
	// BGTZ rs, offset
	else if (i.op == 7) {
		mnemonic("BGTZ");
		disasm("r%d, 0x%x", i.rs, i.imm);

		if (cpu->reg[i.rs] > 0) {
			cpu->shouldJump = true;
			isJumpCycle = false;
			cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
		}
	}

	// Branch On Less Than Or Equal To Zero
	// BLEZ rs, offset

	else if (i.op == 6) {
		mnemonic("BLEZ");
		disasm("r%d, 0x%x", i.rs, i.imm);

		if (cpu->reg[i.rs] == 0 || (cpu->reg[i.rs] & 0x80000000)) {
			cpu->shouldJump = true;
			isJumpCycle = false;
			cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
		}
	}


	else if (i.op == 1) {
		// Branch On Greater Than Or Equal To Zero
		// BGEZ rs, offset
		if (i.rt == 1) {
			mnemonic("BGEZ");
			disasm("r%d, 0x%x", i.rs, i.imm);

			if ((int32_t)cpu->reg[i.rs] >= 0) {
				cpu->shouldJump = true;
				isJumpCycle = false;
				cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
			}
		}

		// Branch On Greater Than Or Equal To Zero And Link
		// BGEZAL rs, offset
		else if (i.rt == 17) {
			mnemonic("BGEZAL");
			disasm("r%d, 0x%x", i.rs, i.imm);

			cpu->reg[31] = cpu->PC + 4;
			if ((int32_t)cpu->reg[i.rs] >= 0) {
				cpu->shouldJump = true;
				isJumpCycle = false;
				cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
			}
		}

		// Branch On Less Than Zero
		// BLTZ rs, offset
		else if (i.rt == 0) {
			mnemonic("BLTZ");
			disasm("r%d, 0x%x", i.rs, i.imm);

			if (cpu->reg[i.rs] & 0x80000000) {
				cpu->shouldJump = true;
				isJumpCycle = false;
				cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
			}
		}

		else return false;
	}

	// Branch On Not Equal
	// BGTZ rs, offset
	else if (i.op == 5) {
		mnemonic("BNE");
		disasm("r%d, r%d, 0x%x", i.rs, i.rt, i.imm);

		if (cpu->reg[i.rt] != cpu->reg[i.rs]) {
			cpu->shouldJump = true;
			isJumpCycle = false;
			cpu->jumpPC = (cpu->PC & 0xf0000000) | ((cpu->PC) + (i.offset << 2));
		}
	}

	// And Immediate
	// ANDI rt, rs, imm
	else if (i.op == 12) {
		mnemonic("ANDI");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.imm);
		pseudo("r%d = r%d & 0x%x", i.rt, i.rs, i.imm);

		cpu->reg[i.rt] = cpu->reg[i.rs] & i.imm;
	}

	// Xor Immediate
	// XORI rt, rs, imm
	else if (i.op == 14) {
		mnemonic("XORI");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.imm);
		pseudo("r%d = r%d ^ 0x%x", i.rt, i.rs, i.imm);

		cpu->reg[i.rt] = cpu->reg[i.rs] ^ i.imm;
	}

	// Add Immediate Unsigned Word
	// ADDIU rt, rs, imm
	else if(i.op == 9) {
		mnemonic("ADDIU");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.offset);
		pseudo("r%d = r%d + %d", i.rt, i.rs, i.offset);

		cpu->reg[i.rt] = cpu->reg[i.rs] + i.offset;
	}

	// Add Immediate Word
	// ADDI rt, rs, imm
	else if (i.op == 8) {
		mnemonic("ADDI");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.offset);

		cpu->reg[i.rt] = cpu->reg[i.rs] + i.offset;
	}

	// Or Immediete
	// ORI rt, rs, imm
	else if (i.op == 13) {
		mnemonic("ORI");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.imm);
		pseudo("r%d = (r%d&0xffff0000) | 0x%x", i.rt, i.rs, i.imm);

		cpu->reg[i.rt] = (cpu->reg[i.rs] & 0xffff0000) | i.imm;
	}

	// Load Upper Immediate
	// LUI rt, imm
	else if (i.op == 15) {
		mnemonic("LUI");
		disasm("r%d, 0x%x", i.rt, i.imm);
		pseudo("r%d = 0x%x", i.rt, i.imm << 16);

		cpu->reg[i.rt] = i.imm << 16;
	}

	// Load Byte
	// LB rt, offset(base)
	else if (i.op == 32) {
		mnemonic("LB");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);

		addr = cpu->reg[i.rs] + i.offset;
		cpu->reg[i.rt] = ((int32_t)(readMemory8(addr) << 24)) >> 24;
	}

	// Load Byte Unsigned
	// LBU rt, offset(base)
	else if (i.op == 36) {
		mnemonic("LBU");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);

		addr = cpu->reg[i.rs] + i.offset;
		cpu->reg[i.rt] = readMemory8(addr);
	}

	// Load Word
	// LW rt, offset(base)
	else if (i.op == 35) {
		mnemonic("LW");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);

		addr = cpu->reg[i.rs] + i.offset;
		cpu->reg[i.rt] = readMemory32(addr);
	}

	// Load Halfword Unsigned
	// LHU rt, offset(base)
	else if (i.op == 33) {
		mnemonic("LH");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);

		addr = cpu->reg[i.rs] + i.offset;
		cpu->reg[i.rt] = ((int32_t)(readMemory16(addr) << 16)) >> 16;
	}

	// Load Halfword Unsigned
	// LHU rt, offset(base)
	else if (i.op == 37) {
		mnemonic("LHU");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);

		addr = cpu->reg[i.rs] + i.offset;
		cpu->reg[i.rt] = readMemory16(addr);
	}

	// Store Byte
	// SB rt, offset(base)
	else if (i.op == 40) {
		mnemonic("SB");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);
		addr = cpu->reg[i.rs] + i.offset;
		writeMemory8(addr, cpu->reg[i.rt]);
	}

	// Store Halfword
	// SH rt, offset(base)
	else if (i.op == 41) {
		mnemonic("SH");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);
		addr = cpu->reg[i.rs] + i.offset;
		writeMemory16(addr, cpu->reg[i.rt]);
	}

	// Store Word
	// SW rt, offset(base)
	else if (i.op == 43) {
		mnemonic("SW");
		disasm("r%d, %d(r%d)", i.rt, i.offset, i.rs);
		addr = cpu->reg[i.rs] + i.offset;
		writeMemory32(addr, cpu->reg[i.rt]);
		pseudo("mem[r%d+0x%x] = r%d    mem[0x%x] = 0x%x", i.rs, i.offset, i.rt, addr, cpu->reg[i.rt]);
	}

	// Set On Less Than Immediate Unsigned
	// SLTI rd, rs, rt
	else if (i.op == 10) {
		mnemonic("SLTI");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.imm);

		if ((int32_t)cpu->reg[i.rs] < i.offset) cpu->reg[i.rt] = 1;
		else cpu->reg[i.rt] = 0;
	}

	// Set On Less Than Immediate Unsigned
	// SLTIU rd, rs, rt
	else if (i.op == 11) {
		mnemonic("SLTIU");
		disasm("r%d, r%d, 0x%x", i.rt, i.rs, i.imm);

		if (cpu->reg[i.rs] < i.imm) cpu->reg[i.rt] = 1;
		else cpu->reg[i.rt] = 0;
	}

	// Coprocessor zero
	else if (i.op == 16) {
		// Move from co-processor zero
		// MFC0 rd, <nn>
		if (i.rs == 0)
		{
			mnemonic("MFC0");
			disasm("r%d, $%d", i.rt, i.rd);

			uint32_t tmp = cpu->COP0[i.rd];
			cpu->reg[i.rt] = tmp;
		}

		// Move to co-processor zero
		// MTC0 rs, <nn>
		else if (i.rs == 4)
		{
			mnemonic("MTC0");
			disasm("r%d, $%d", i.rt, i.rd);

			uint32_t tmp = cpu->reg[i.rt];
			cpu->COP0[i.rd] = tmp;
			if (i.rd == 12) IsC = (tmp & 0x10000) ? true : false;
		}
	}

	// R Type
	else if (i.op == 0) {
		// Jump Register
		// JR rs
		if (i.fun == 8) {
			mnemonic("JR");
			disasm("r%d", i.rs);
			cpu->shouldJump = true;
			isJumpCycle = false;
			cpu->jumpPC = cpu->reg[i.rs];
		}

		// Jump Register
		// JALR
		else if (i.fun == 9) {
			mnemonic("JALR");
			disasm("r%d r%d", i.rd, i.rs);
			cpu->shouldJump = true;
			isJumpCycle = false;
			cpu->jumpPC = cpu->reg[i.rs];
			cpu->reg[i.rd] = cpu->PC + 4;
		}

		// Add
		// add rd, rs, rt
		else if (i.fun == 32) {
			mnemonic("ADD");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);
			cpu->reg[i.rd] = ((int32_t)cpu->reg[i.rs]) + ((int32_t)cpu->reg[i.rt]);
		}

		// Add unsigned
		// add rd, rs, rt
		else if (i.fun == 33) {
			mnemonic("ADDU");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);
			cpu->reg[i.rd] = ((int32_t)cpu->reg[i.rs]) + ((int32_t)cpu->reg[i.rt]);
		}

		// And
		// and rd, rs, rt
		else if (i.fun == 36) {
			mnemonic("AND");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);

			cpu->reg[i.rd] = cpu->reg[i.rs] & cpu->reg[i.rt];
		}


		// TODO
		// Multiply
		// mul rs, rt
		else if (i.fun == 24) {
			mnemonic("MULT");
			disasm("r%d, r%d", i.rs, i.rt);

			uint64_t temp = cpu->reg[i.rs] * cpu->reg[i.rt];

			cpu->lo = temp & 0xffffffff;
			cpu->hi = (temp & 0xffffffff00000000) >> 32;
		}


		// Nor
		// NOR rd, rs, rt
		else if (i.fun == 39) {
			mnemonic("OR");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);

			cpu->reg[i.rd] = ~(cpu->reg[i.rs] | cpu->reg[i.rt]);
		}

		// Or
		// OR rd, rs, rt
		else if (i.fun == 37) {
			mnemonic("OR");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);

			cpu->reg[i.rd] = cpu->reg[i.rs] | cpu->reg[i.rt];
		}

		// Shift Word Left Logical
		// SLL rd, rt, a
		else if (i.fun == 0) {
			if (i.rt == 0 && i.rd == 0 && i.sh == 0) {
				mnemonic("NOP");
				disasm(" ");
			}
			else {
				mnemonic("SLL");
				disasm("r%d, r%d, %d", i.rd, i.rt, i.sh);

				cpu->reg[i.rd] = cpu->reg[i.rt] << i.sh;
			}
		}

		// Shift Word Left Logical Variable
		// SLLV rd, rt, rs
		else if (i.fun == 4) {
			mnemonic("SLLV");
			disasm("r%d, r%d, r%d", i.rd, i.rt, i.rs);

			cpu->reg[i.rd] = cpu->reg[i.rt] << (cpu->reg[i.rs] & 0x1f);
		}

		// Shift Word Right Arithmetic
		// SRA rd, rt, a
		else if (i.fun == 3) {
			mnemonic("SRA");
			disasm("r%d, r%d, %d", i.rd, i.rt, i.sh);

			cpu->reg[i.rd] = ((int32_t)cpu->reg[i.rt]) >> i.sh;
		}

		// Shift Word Right Arithmetic Variable
		// SRAV rd, rt, rs
		else if (i.fun == 7) {
			mnemonic("SRAV");
			disasm("r%d, r%d, r%d", i.rd, i.rt, i.rs);

			cpu->reg[i.rd] = ((int32_t)cpu->reg[i.rt]) >> (cpu->reg[i.rs] & 0x1f);
		}

		// Shift Word Right Logical
		// SRL rd, rt, a
		else if (i.fun == 2) {
			mnemonic("SRL");
			disasm("r%d, r%d, %d", i.rd, i.rt, i.sh);

			cpu->reg[i.rd] = cpu->reg[i.rt] >> i.sh;
		}

		// Shift Word Right Logical Variable
		// SRLV rd, rt, a
		else if (i.fun == 6) {
			mnemonic("SRLV");
			disasm("r%d, r%d, %d", i.rd, i.rt, i.sh);

			cpu->reg[i.rd] = cpu->reg[i.rt] >> (cpu->reg[i.rs] & 0x1f);
		}
		
		// Xor
		// XOR rd, rs, rt
		else if (i.fun == 38) {
			mnemonic("XOR");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);

			cpu->reg[i.rd] = cpu->reg[i.rs] ^ cpu->reg[i.rt];
		}

		// Subtract
		// sub rd, rs, rt
		else if (i.fun == 34) {
			mnemonic("SUB");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);
			cpu->reg[i.rd] = ((int32_t)cpu->reg[i.rs]) - ((int32_t)cpu->reg[i.rt]);
		}

		// Subtract unsigned
		// subu rd, rs, rt
		else if (i.fun == 35) {
			mnemonic("SUBU");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);
			cpu->reg[i.rd] = ((int32_t)cpu->reg[i.rs]) - ((int32_t)cpu->reg[i.rt]);
		}


		// Set On Less Than Signed
		// SLT rd, rs, rt
		else if (i.fun == 42) {
			mnemonic("SLTU");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);

			if ((int32_t)cpu->reg[i.rs] < (int32_t)cpu->reg[i.rt]) cpu->reg[i.rd] = 1;
			else cpu->reg[i.rd] = 0;
		}

		// Set On Less Than Unsigned
		// SLTU rd, rs, rt
		else if (i.fun == 43) {
			mnemonic("SLTU");
			disasm("r%d, r%d, r%d", i.rd, i.rs, i.rt);

			if (cpu->reg[i.rs] < cpu->reg[i.rt]) cpu->reg[i.rd] = 1;
			else cpu->reg[i.rd] = 0;
		}

		// Divide
		// div rs, rt
		else if (i.fun == 26) {
			mnemonic("DIV");
			disasm("r%d, r%d", i.rs, i.rt);

			cpu->lo = (int32_t)cpu->reg[i.rs] / (int32_t)cpu->reg[i.rt];
			cpu->hi = (int32_t)cpu->reg[i.rs] % (int32_t)cpu->reg[i.rt];
		}

		// Divide Unsigned Word
		// divu rs, rt
		else if (i.fun == 27) {
			mnemonic("DIVU");
			disasm("r%d, r%d", i.rs, i.rt);

			cpu->lo = cpu->reg[i.rs] / cpu->reg[i.rt];
			cpu->hi = cpu->reg[i.rs] % cpu->reg[i.rt];
		}

		// Move From Hi
		// MFHI rd
		else if (i.fun == 16) {
			mnemonic("MFHI");
			disasm("r%d", i.rd);

			cpu->reg[i.rd] = cpu->hi;
		}

		// Move From Lo
		// MFLO rd
		else if (i.fun == 18) {
			mnemonic("MFLO");
			disasm("r%d", i.rd);

			cpu->reg[i.rd] = cpu->lo;
		}

		// Move To Lo
		// MTLO rd
		else if (i.fun == 19) {
			mnemonic("MTLO");
			disasm("r%d", i.rd);

			cpu->lo = cpu->reg[i.rd];
		}

		// Move To Hi
		// MTHI rd
		else if (i.fun == 17) {
			mnemonic("MTHI");
			disasm("r%d", i.rd);

			cpu->hi = cpu->reg[i.rd];
		}

		// Syscall
		// SYSCALL
		else if (i.fun == 12) {
			//__debugbreak();
			//printf("Syscall: r4: 0x%x\n", cpu->reg[4]);
			mnemonic("SYSCALL");
			disasm("");

			cpu->COP0[14] = cpu->PC;// EPC - retur address from trap
			cpu->COP0[13] = 8<<2;// Cause, hardcoded SYSCALL
			cpu->PC = 0x80000080;
		}

		else return false;
		// TODO: break
		// TODO: multu
	}
	else return false;

#undef b

	if (disassemblyEnabled) {
		if (_disasm.empty()) printf("%s\n", _mnemonic.c_str());
		else {
			if (!_pseudo.empty())
				printf("   0x%08x  %08x:    %s\n", cpu->PC - 4, readMemory32(cpu->PC - 4), _pseudo.c_str());
			else
				printf("   0x%08x  %08x:    %s\n", cpu->PC - 4, readMemory32(cpu->PC - 4), _disasm.c_str());

			//std::transform(_mnemonic.begin(), _mnemonic.end(), _mnemonic.begin(), ::tolower);
			//printf("%08X %08X %-7s %s\n", cpu->PC - 4, readMemory32(cpu->PC - 4), _mnemonic.c_str(), _disasm.c_str());
		}
	}

	if (cpu->shouldJump && isJumpCycle) {
		cpu->PC = cpu->jumpPC & 0xFFFFFFFC;
		cpu->jumpPC = 0;
		cpu->shouldJump = false;
	}
	return true;
}

std::string getStringFromRam(uint32_t addr)
{
	std::string format;

	int c;
	int i = 0;
	while (1)
	{
		c = readMemory(addr);
		addr++;
		if (!c) break;
		format += c;
		if (i++ > 100) break;
	}
	return format;
}

bool doDump = false;
int main( int argc, char** argv )
{
	CPU cpu;
	bool cpuRunning = true;
	memset(cpu.reg, 0, sizeof(uint32_t) * 32);
	
	std::string biosPath = "data/bios/SCPH1000.bin";
	
	auto _bios = getFileContents(biosPath);
	if (_bios.empty()) {
		printf("Cannot open BIOS");
		return 1;
	}
	memcpy(bios, &_bios[0], _bios.size());

	// Pre boot hook
	//uint32_t preBootHookAddress = 0x1F000100;
	//std::string license = "Licensed by Sony Computer Entertainment Inc.";
	//memcpy(&expansion[0x84], license.c_str(), license.size());

	int cycles = 0;
	int frames = 0;

	uint32_t instruction = readMemory32(cpu.PC);
	Opcode _opcode;

	while (cpuRunning)
	{
		_opcode.opcode = readMemory32(cpu.PC);
		//if (cpu.PC == 0xbfc018d0)
		//{
		//	disassemblyEnabled = true;
		//	__debugbreak();
		//	std::string format = getStringFromRam(cpu.reg[4]);
		//	printf("___%s\n", format.c_str());
		//}

		cpu.PC += 4;

		bool executed = executeInstruction(&cpu, _opcode);
		cycles += 2;

		if (cycles >= 564480) {
			cycles = 0;
			frames++;
			printf("Frame: %d\n", frames);
		}

		if (doDump)
		{
			std::vector<uint8_t> ramdump;
			ramdump.resize(2 * 1024 * 1024);
			memcpy(&ramdump[0], ram, ramdump.size());
			putFileContents("ram.bin", ramdump);
			doDump = false;
		}
		if (!executed)
		{
			printf("Unknown instruction @ 0x%08x: 0x%08x (copy: %02x %02x %02x %02x)\n", cpu.PC - 4, _opcode.opcode, _opcode.opcode & 0xff, (_opcode.opcode >> 8) & 0xff, (_opcode.opcode >> 16) & 0xff, (_opcode.opcode >> 24) & 0xff);
			cpuRunning = false;
		}
	}

	return 0;
}