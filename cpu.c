#include "cpu_common.h"

/* 每个阶段通过 CPU 结构体中的暂存值传递数据。请补全以下六个阶段。 */
void fetch(CPU *cpu)
{
    uint64_t byte0 = read_mem(cpu, cpu->pc, 1);
    if (cpu->stat != AOK) return;

    cpu->icode = (int)(byte0 >> 4);
    cpu->ifun = (int)(byte0 & 0xF);

    int valid = 1;

    if (cpu->icode > I_POPQ) {
        valid = 0;
    } else if (cpu->icode == I_RRMOVQ ||
               cpu->icode == I_JMP) {
        valid = cpu->ifun <= 6;
    } else if (cpu->icode == I_ALU) {
        valid = cpu->ifun <= 3;
    } else {
        valid = cpu->ifun == 0;
    }

    if (!valid) {
        cpu->stat = INS;
        return;
    }

    int need_reg =
        cpu->icode == I_RRMOVQ ||
        cpu->icode == I_IRMOVQ ||
        cpu->icode == I_RMMOVQ ||
        cpu->icode == I_MRMOVQ ||
        cpu->icode == I_ALU ||
        cpu->icode == I_PUSHQ ||
        cpu->icode == I_POPQ;

    int need_valC =
        cpu->icode == I_IRMOVQ ||
        cpu->icode == I_RMMOVQ ||
        cpu->icode == I_MRMOVQ ||
        cpu->icode == I_JMP ||
        cpu->icode == I_CALL;

    cpu->rA = cpu->rB = RNONE;
    cpu->valC = 0;

    uint64_t next = cpu->pc + 1;

    if (need_reg) {
        uint64_t reg_byte = read_mem(cpu, next, 1);
        if (cpu->stat != AOK) return;

        cpu->rA = (int)(reg_byte >> 4);
        cpu->rB = (int)(reg_byte & 0xF);

        if (cpu->icode == I_IRMOVQ) {
            valid = cpu->rA == RNONE && cpu->rB != RNONE;
        } else if (cpu->icode == I_PUSHQ ||
                   cpu->icode == I_POPQ) {
            valid = cpu->rA != RNONE && cpu->rB == RNONE;
        } else {
            valid = cpu->rA != RNONE && cpu->rB != RNONE;
        }

        if (!valid) {
            cpu->stat = INS;
            return;
        }

        next++;
    }

    if (need_valC) {
        cpu->valC = read_mem(cpu, next, 8);
        if (cpu->stat != AOK) return;

        next += 8;
    }

    cpu->valP = next;

    if (cpu->icode == I_HALT) {
        cpu->stat = HLT;
        return;
    }
}

void decode(CPU *cpu)
{
    int srcA = RNONE;
    int srcB = RNONE;

    switch (cpu->icode) {
        case I_RRMOVQ:
        case I_RMMOVQ:
        case I_ALU:
        case I_PUSHQ:
            srcA = cpu->rA;
            break;

        case I_POPQ:
        case I_RET:
            srcA = RSP;
            break;
    }

    switch (cpu->icode) {
        case I_RMMOVQ:
        case I_MRMOVQ:
        case I_ALU:
            srcB = cpu->rB;
            break;

        case I_PUSHQ:
        case I_POPQ:
        case I_CALL:
        case I_RET:
            srcB = RSP;
            break;
    }

    cpu->valA = (srcA == RNONE) ? 0 : cpu->reg[srcA];
    cpu->valB = (srcB == RNONE) ? 0 : cpu->reg[srcB];
}


void execute(CPU *cpu)
{
    cpu->valE = 0;
    cpu->cnd = 0;

    switch (cpu->icode) {
        case I_RRMOVQ:
            cpu->valE = cpu->valA;
            cpu->cnd = check_cond(cpu, cpu->ifun);
            break;

        case I_IRMOVQ:
            cpu->valE = cpu->valC;
            break;

        case I_RMMOVQ:
        case I_MRMOVQ:
            cpu->valE = cpu->valB + cpu->valC;
            break;

        case I_ALU: {
            uint64_t a = cpu->valA;
            uint64_t b = cpu->valB;
            uint64_t result = 0;
            int overflow = 0;

            switch (cpu->ifun) {
                case 0: // addq
                    result = b + a;
                    overflow =
                        ((~(a ^ b) & (result ^ b)) >> 63) & 1;
                    break;

                case 1: // subq
                    result = b - a;
                    overflow =
                        (((a ^ b) & (result ^ b)) >> 63) & 1;
                    break;

                case 2: // andq
                    result = b & a;
                    break;

                case 3: // xorq
                    result = b ^ a;
                    break;
            }

            cpu->valE = result;
            cpu->zf = (result == 0);
            cpu->sf = (int)(result >> 63);
            cpu->of = overflow;
            break;
        }

        case I_JMP:
            cpu->cnd = check_cond(cpu, cpu->ifun);
            break;

        case I_CALL:
        case I_PUSHQ:
            cpu->valE = cpu->valB - 8;
            break;

        case I_RET:
        case I_POPQ:
            cpu->valE = cpu->valB + 8;
            break;
    }
}



void memory_stage(CPU *cpu)
{
    cpu->valM = 0;

    switch (cpu->icode) {

        case I_RMMOVQ:
            write_mem(cpu, cpu->valE, 8, cpu->valA);
            break;

        case I_MRMOVQ:
            cpu->valM = read_mem(cpu, cpu->valE, 8);
            break;

        case I_PUSHQ:
            write_mem(cpu, cpu->valE, 8, cpu->valA);
            break;

        case I_POPQ:
            cpu->valM = read_mem(cpu, cpu->valA, 8);
            break;

        case I_CALL:
            write_mem(cpu, cpu->valE, 8, cpu->valP);
            break;

        case I_RET:
            cpu->valM = read_mem(cpu, cpu->valA, 8);
            break;
    }
}



void write_back(CPU *cpu)
{
    int dstE = RNONE;
    int dstM = RNONE;

    switch (cpu->icode) {
        case I_RRMOVQ:
            if (cpu->cnd)
                dstE = cpu->rB;
            break;

        case I_IRMOVQ:
        case I_MRMOVQ:
        case I_ALU:
            dstE = cpu->rB;
            break;

        case I_PUSHQ:
        case I_POPQ:
        case I_CALL:
        case I_RET:
            dstE = RSP;
            break;
    }

    switch (cpu->icode) {
        case I_MRMOVQ:
        case I_POPQ:
            dstM = cpu->rA;
            break;
    }

    if (dstE != RNONE)
        cpu->reg[dstE] = cpu->valE;

    if (dstM != RNONE)
        cpu->reg[dstM] = cpu->valM;
}



void update_pc(CPU *cpu)
{
    switch (cpu->icode) {
        case I_CALL:
            cpu->pc = cpu->valC;
            break;

        case I_JMP:
            cpu->pc = cpu->cnd ? cpu->valC : cpu->valP;
            break;

        case I_RET:
            cpu->pc = cpu->valM;
            break;

        default:
            cpu->pc = cpu->valP;
            break;
    }
}
