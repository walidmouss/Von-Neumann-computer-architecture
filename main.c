#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>



uint32_t memory[2048]; // 2048 words of 32-bit memory
uint32_t registers[32]; // 32 registers, each 32 bits
uint32_t PC = 0;
int cycle = 1;

// pipeline buckets: which instruction (by ID) sits in each stage
uint32_t IF_stage  = 0;
uint32_t ID_stage  = 0;
uint32_t EX_stage  = 0;
uint32_t MEM_stage = 0;
uint32_t WB_stage  = 0;


void execute_Rtype(uint32_t instruction) {
    printf("this is an R type instruction");
}
void execute_Itype(uint32_t instruction) {
    printf("this is an I type instruction");

}
void execute_Jtype(uint32_t instruction) {
    printf("this is a J type instruction");

}

void fetch() {
    if (PC < 2048) {
        IF_stage = memory[PC];   // grab the 32nd bit word at address PC and put it into the IF bucket
        PC++;                          // point PC to the next instruction
    }
    else{
        printf("oops, i think you overstepped a little ... we reached end of memory");
    }
}




// this moves the content of each bucket to the next one
void clock_pulse() {
    WB_stage  = MEM_stage;  // the instr in MEM moves to WB
    MEM_stage = EX_stage;   // EX → MEM
    EX_stage  = ID_stage;   // ID → EX
    ID_stage  = IF_stage;   // IF → ID
    IF_stage  = 0;          // clear the IF bucket
    if (cycle % 2 == 1) {
        fetch(); // fetch on odd-numbered cycles bas
    }
    cycle ++ ;
}


void execute_instruction(uint32_t instruction) {
    uint32_t opcode = (instruction >> 28) & 0xF;

    if (opcode == 0 || opcode == 1 || opcode == 8 || opcode == 9) {
        execute_Rtype(instruction);
    }
    else if (opcode == 2 || opcode == 3 || opcode == 4 || opcode == 5 ||
             opcode == 6 || opcode == 10 || opcode == 11) {
        execute_Itype(instruction);
    }
    else if (opcode == 7) {
        execute_Jtype(instruction);
    }
    else {
        printf("Unknown opcode: %x\n", opcode);

    }
}



int main()
{
    memory[0] = 0x10000000; // Dummy opcode
    memory[1] = 0x20000000; // Another dummy opcode
     for (int i = 0; i < 10; i++) {
        clock_pulse();
        printf("Cycle %d: IF=%08X ID=%08X EX=%08X MEM=%08X WB=%08X\n", cycle-1, IF_stage, ID_stage, EX_stage, MEM_stage, WB_stage);

    }
}
