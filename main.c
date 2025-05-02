#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>



typedef struct {
    uint32_t opcode;
    uint32_t R1, R2, R3;
    uint32_t shift;
    uint32_t immediate;
    uint32_t address;
    uint32_t val2, val3;
    char type;
} DecodedInstruction;

typedef struct {
    uint32_t raw;
    DecodedInstruction decoded;
} PipelineRegister;



uint32_t memory[2048]; // 2048 words of 32-bit memory
uint32_t registers[32]; // 32 registers, each 32 bits
uint32_t PC = 0;
int cycle = 1;

// pipeline buckets: which instruction (by ID) sits in each stage
PipelineRegister IF_stage  = {0};
PipelineRegister ID_stage  = {0};
PipelineRegister EX_stage = {0};
PipelineRegister MEM_stage = {0};
PipelineRegister WB_stage  = {0};

void decode(){
    DecodedInstruction current_decoded;
    current_decoded.type = 'U';

    current_decoded.opcode = (ID_stage.raw >> 28) & 0xF; // bring the instruction from fetch phase and add its opcode to the .opcode part

    switch (current_decoded.opcode) {
        // R-type
        case 0: case 1: case 8: case 9:
            current_decoded.type = 'R';
            current_decoded.R1 = (ID_stage.raw >> 23) & 0x1F;
            current_decoded.R2 = (ID_stage.raw >> 18) & 0x1F;
            current_decoded.R3 = (ID_stage.raw >> 13) & 0x1F;
            current_decoded.shift = ID_stage.raw & 0x1FFF;
            current_decoded.val2 = registers[current_decoded.R2];
            current_decoded.val3 = registers[current_decoded.R3];
            break;

        // I-type
        case 2: case 3: case 4: case 5: case 6: case 10: case 11:
            current_decoded.type = 'I';
            current_decoded.R1 = (ID_stage.raw >> 23) & 0x1F;
            current_decoded.R2 = (ID_stage.raw >> 18) & 0x1F;
            current_decoded.immediate = ID_stage.raw & 0x3FFFF; // 18-bit
            current_decoded.val2 = registers[current_decoded.R2];
            break;

        // J-type
        case 7:
            current_decoded.type = 'J';
            current_decoded.address = ID_stage.raw & 0x0FFFFFFF;
            break;

        default:
            printf("Unknown opcode in decode: %X\n", current_decoded.opcode);
    }
    ID_stage.decoded = current_decoded;

}



void fetch() {
    if (PC < 2048) {
        IF_stage.raw = memory[PC];   // grab the 32nd bit word at address PC and put it into the IF bucket
        PC++;                          // point PC to the next instruction
    }
    else{
        printf("oops, i think you overstepped a little ... we reached end of memory");
    }
}



void execute_instruction(PipelineRegister instruction) {
    uint32_t opcode = (instruction.raw >> 28) & 0xF;
    DecodedInstruction currDecodedInst = instruction.decoded;
    switch(opcode){
        case(0):
            registers[currDecodedInst.R1] = currDecodedInst.val2 + currDecodedInst.val3; // add 2 registers and store in mem[R1]
            break;
        case(1):
            registers[currDecodedInst.R1] = currDecodedInst.val2 - currDecodedInst.val3; // subtract 2 registers and store in mem[R1]
            break;
        case(2):
            registers[currDecodedInst.R1] = currDecodedInst.val2 * currDecodedInst.immediate;
            break;
        case(3):
            registers[currDecodedInst.R1] = currDecodedInst.val2 + currDecodedInst.immediate;
            break;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////HERE BNE IS NOT IMPLEMENTED /////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        case(5):
            registers[currDecodedInst.R1] = currDecodedInst.val2 & currDecodedInst.immediate;
            break;
        case(6):
            registers[currDecodedInst.R1] = currDecodedInst.val2 | currDecodedInst.immediate;
            break;
        case(8):
            registers[currDecodedInst.R1] = currDecodedInst.val2 << currDecodedInst.shift; // shift left
            break;
        case(9):
            registers[currDecodedInst.R1] = currDecodedInst.val2 >> currDecodedInst.shift; // shift left
            break;
        default:printf("Unknown opcode: %x\n", opcode);
    }
}




// this moves the content of each bucket to the next one
void clock_pulse() {
    WB_stage  = MEM_stage;  // the instr in MEM moves to WB
    MEM_stage = EX_stage;   // EX → MEM
    if (EX_stage.raw != 0) {
        execute_instruction(EX_stage);
    }
    EX_stage  = ID_stage;   // ID → EX
    ID_stage  = IF_stage;   // IF → ID
    if (ID_stage.raw != 0) {
        decode();
    }
    memset(&IF_stage, 0, sizeof(IF_stage));     // clear the IF bucket



    if (cycle % 2 == 1) {
        fetch(); // fetch on odd-numbered cycles bas
    }
    cycle ++ ;
}




int main()
{
    // Test input setup

// Instruction 1: ADD R2, R3 -> R1
memory[0] = (0 << 28) | (1 << 23) | (2 << 18) | (3 << 13); // ADD R2, R3 -> R1

// Instruction 2: ADDI R2, 0x1234 -> R1
memory[1] = (2 << 28) | (1 << 23) | (2 << 18) | 0x1234; // ADDI R2, 0x1234 -> R1

// Instruction 3: JUMP to address 0xAB
memory[2] = (7 << 28) | 0xAB; // JUMP to 0xAB

// Set initial values for registers
registers[2] = 100;  // 0x00000064
registers[3] = 200;  // 0x000000C8


     for (int i = 0; i < 10; i++) {
        clock_pulse();
        printf("Cycle %d: IF=%08X ID=%08X EX=%08X MEM=%08X WB=%08X\n",
            cycle-1,
            IF_stage.raw,
            ID_stage.raw,
            EX_stage.raw,
            MEM_stage.raw,
            WB_stage.raw
        );

    }
}
