#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// --- Configuration (Package 1 Specific) ---
#define MEMORY_SIZE 2048
#define NUM_REGISTERS 32
#define INSTRUCTION_MEM_END 1023
#define DATA_MEM_START 1024

// --- Opcodes (Package 1) ---
#define OPCODE_ADD  0
#define OPCODE_SUB  1
#define OPCODE_MULI 2
#define OPCODE_ADDI 3
#define OPCODE_BNE  4
#define OPCODE_ANDI 5
#define OPCODE_ORI  6
#define OPCODE_J    7
#define OPCODE_SLL  8
#define OPCODE_SRL  9
#define OPCODE_LW   10
#define OPCODE_SW   11
#define OPCODE_NOP  15

// --- Structures ---
typedef struct {
    uint32_t opcode;     // 4 bits
    uint32_t R1_idx, R2_idx, R3_idx; // Register indices
    uint32_t shamt;      // Shift amount (for SLL, SRL)
    int32_t  immediate;  // Sign-extended immediate
    uint32_t address;    // For J-type
    int32_t val_R1_source;
    int32_t val_R2_source;
    int32_t val_R3_source;
    int32_t alu_result;
    int32_t mem_read_val;
    char type; // 'R', 'I', 'J', 'N' (NOP), 'U' (Unknown/Undecoded)
    int original_pc;
} DecodedInstruction;

typedef struct {
    uint32_t raw_instruction;
    DecodedInstruction decoded_info;
    uint8_t cycles_spent_in_stage;
    int instruction_pc_at_fetch;
    bool valid;
} PipelineRegister;

// --- Global State ---
uint32_t memory[MEMORY_SIZE];
int32_t  registers[NUM_REGISTERS];
int32_t  PC = 0;
int      current_cycle = 0;

PipelineRegister active_in_IF_stage  = {.valid = false};
PipelineRegister active_in_ID_stage  = {.valid = false};
PipelineRegister active_in_EX_stage  = {.valid = false};
PipelineRegister active_in_MEM_stage = {.valid = false};
PipelineRegister active_in_WB_stage  = {.valid = false};

int halt_simulation = 0;
int instructions_loaded_count = 0;

bool can_IF_operate_this_cycle = false;
bool can_MEM_operate_this_cycle = false;
bool branch_taken_in_EX_cycle2 = false;
uint32_t branch_target_pc = 0;
bool stall_IF_for_mem_after_branch = false;
bool hazard_detected = false; // New flag for load-use hazard stalling

// --- Helper: Get Opcode Name ---
const char* get_opcode_name(uint8_t opcode_val) {
    switch (opcode_val) {
        case OPCODE_ADD: return "ADD";  case OPCODE_SUB: return "SUB";
        case OPCODE_MULI:return "MULI"; case OPCODE_ADDI:return "ADDI";
        case OPCODE_BNE: return "BNE";  case OPCODE_ANDI:return "ANDI";
        case OPCODE_ORI: return "ORI";  case OPCODE_J:   return "J";
        case OPCODE_SLL: return "SLL";  case OPCODE_SRL: return "SRL";
        case OPCODE_LW:  return "LW";   case OPCODE_SW:  return "SW";
        case OPCODE_NOP: return "NOP";
        default: return "UNK";
    }
}

// --- Initialize Processor ---
void initialize_processor() {
    PC = 0;
    current_cycle = 0;
    halt_simulation = 0;
    instructions_loaded_count = 0;
    memset(memory, 0, sizeof(memory));
    for(int i=0; i<NUM_REGISTERS; ++i) registers[i] = 0;

    active_in_IF_stage.valid = false;
    active_in_ID_stage.valid = false;
    active_in_EX_stage.valid = false;
    active_in_MEM_stage.valid = false;
    active_in_WB_stage.valid = false;

    branch_taken_in_EX_cycle2 = false;
    stall_IF_for_mem_after_branch = false;
    hazard_detected = false;
}

// --- Helper Functions for Parsing ---
uint8_t get_opcode(const char* opcode_str) {
    if (strcmp(opcode_str, "ADD") == 0) return OPCODE_ADD;
    else if (strcmp(opcode_str, "SUB") == 0) return OPCODE_SUB;
    else if (strcmp(opcode_str, "MULI") == 0) return OPCODE_MULI;
    else if (strcmp(opcode_str, "ADDI") == 0) return OPCODE_ADDI;
    else if (strcmp(opcode_str, "BNE") == 0) return OPCODE_BNE;
    else if (strcmp(opcode_str, "ANDI") == 0) return OPCODE_ANDI;
    else if (strcmp(opcode_str, "ORI") == 0) return OPCODE_ORI;
    else if (strcmp(opcode_str, "J") == 0) return OPCODE_J;
    else if (strcmp(opcode_str, "SLL") == 0) return OPCODE_SLL;
    else if (strcmp(opcode_str, "SRL") == 0) return OPCODE_SRL;
    else if (strcmp(opcode_str, "LW") == 0) return OPCODE_LW;
    else if (strcmp(opcode_str, "SW") == 0) return OPCODE_SW;
    else if (strcmp(opcode_str, "NOP") == 0) return OPCODE_NOP;
    else {
        printf("Unknown opcode: %s\n", opcode_str);
        exit(1);
    }
}

char get_instruction_type(const char* opcode_str) {
    if (strcmp(opcode_str, "ADD") == 0 || strcmp(opcode_str, "SUB") == 0 ||
        strcmp(opcode_str, "SLL") == 0 || strcmp(opcode_str, "SRL") == 0) {
        return 'R';
    } else if (strcmp(opcode_str, "MULI") == 0 || strcmp(opcode_str, "ADDI") == 0 ||
               strcmp(opcode_str, "BNE") == 0 || strcmp(opcode_str, "ANDI") == 0 ||
               strcmp(opcode_str, "ORI") == 0 || strcmp(opcode_str, "LW") == 0 ||
               strcmp(opcode_str, "SW") == 0) {
        return 'I';
    } else if (strcmp(opcode_str, "J") == 0) {
        return 'J';
    } else if (strcmp(opcode_str, "NOP") == 0) {
        return 'N';
    } else {
        printf("Unknown opcode: %s\n", opcode_str);
        exit(1);
    }
}

int parse_register(const char* reg_str) {
    if (reg_str[0] == 'R') {
        int reg_num = atoi(reg_str + 1);
        if (reg_num >= 0 && reg_num < 32) return reg_num;
    }
    printf("Invalid register: %s\n", reg_str);
    exit(1);
}

int parse_immediate(const char* imm_str) {
    return atoi(imm_str);
}

// --- Load Assembly File ---
void load_assembly_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error opening file: %s\n", filename);
        exit(1);
    }
    int i = 0;
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        // Trim leading whitespace
        char* ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (*ptr == '\n' || *ptr == '\0') continue; // Skip empty lines
        // Tokenize
        char* tokens[5];
        int token_count = 0;
        char* token = strtok(ptr, " \t\n");
        while (token != NULL && token_count < 5) {
            tokens[token_count++] = token;
            token = strtok(NULL, " \t\n");
        }
        if (token_count == 0) continue;
        const char* opcode_str = tokens[0];
        char type = get_instruction_type(opcode_str);
        uint32_t instruction = 0;
        if (type == 'R') {
            if (token_count != 4) {
                printf("Invalid R-type instruction: %s\n", line);
                exit(1);
            }
            uint8_t r1 = parse_register(tokens[1]);
            uint8_t r2 = parse_register(tokens[2]);
            uint32_t shamt = 0;
            uint8_t r3 = 0;
            if (strcmp(opcode_str, "SLL") == 0 || strcmp(opcode_str, "SRL") == 0) {
                shamt = parse_immediate(tokens[3]);
                if (shamt > 0x1FFF) { // 13-bit limit
                    printf("Shift amount too large: %s\n", tokens[3]);
                    exit(1);
                }
            } else {
                r3 = parse_register(tokens[3]);
            }
            uint8_t opcode = get_opcode(opcode_str);
            instruction = (opcode << 28) | (r1 << 23) | (r2 << 18) | (r3 << 13) | shamt;
        } else if (type == 'I') {
            uint8_t r1, r2;
            int32_t imm;
            if (strcmp(opcode_str, "LW") == 0 || strcmp(opcode_str, "SW") == 0) {
                if (token_count != 3) {
                    printf("Invalid LW/SW instruction: %s\n", line);
                    exit(1);
                }
                r1 = parse_register(tokens[1]);
                char* offset_str = strtok(tokens[2], "(");
                char* rs_str = strtok(NULL, ")");
                if (offset_str == NULL || rs_str == NULL) {
                    printf("Invalid memory address format: %s\n", tokens[2]);
                    exit(1);
                }
                imm = parse_immediate(offset_str);
                r2 = parse_register(rs_str);
            } else {
                if (token_count != 4) {
                    printf("Invalid I-type instruction: %s\n", line);
                    exit(1);
                }
                r1 = parse_register(tokens[1]);
                r2 = parse_register(tokens[2]);
                imm = parse_immediate(tokens[3]);
            }
            uint8_t opcode = get_opcode(opcode_str);
            instruction = (opcode << 28) | (r1 << 23) | (r2 << 18) | (imm & 0x3FFFF);
        } else if (type == 'J') {
            if (token_count != 2) {
                printf("Invalid J-type instruction: %s\n", line);
                exit(1);
            }
            uint32_t address = parse_immediate(tokens[1]);
            uint8_t opcode = get_opcode(opcode_str);
            instruction = (opcode << 28) | (address & 0x0FFFFFFF);
        } else if (type == 'N') {
            if (token_count != 1) {
                printf("Invalid NOP instruction: %s\n", line);
                exit(1);
            }
            instruction = (OPCODE_NOP << 28);
        }
        memory[i] = instruction;
        i++;
    }
    instructions_loaded_count = i;
    fclose(file);
    printf("Loaded %d instructions from %s.\n", instructions_loaded_count, filename);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////fetch///////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fetch_instruction_stage_op() {
    if (!can_IF_operate_this_cycle) {
        printf("Cycle %d: IF - Idle (MEM active or stalled).\n", current_cycle);
        active_in_IF_stage.valid = false;
        return;
    }

    if (PC < instructions_loaded_count && PC <= INSTRUCTION_MEM_END) {
        active_in_IF_stage.raw_instruction = memory[PC];
        active_in_IF_stage.instruction_pc_at_fetch = PC;
        active_in_IF_stage.valid = true;
        active_in_IF_stage.cycles_spent_in_stage = 0;
        active_in_IF_stage.decoded_info.original_pc = PC;
        active_in_IF_stage.decoded_info.opcode = (active_in_IF_stage.raw_instruction >> 28) & 0xF;

        printf("Cycle %d: IF - Inputs: PC=%d\n", current_cycle, PC);
        printf("Cycle %d: IF - Fetched instr %d (0x%08X, %s) from Mem[%d].\n",
               current_cycle, PC, active_in_IF_stage.raw_instruction, get_opcode_name(active_in_IF_stage.decoded_info.opcode), PC);
        printf("Cycle %d: IF - Outputs: RawInstr=0x%08X, NextPC=%d\n", current_cycle, active_in_IF_stage.raw_instruction, PC + 1);
        PC++;
    } else {
        if (PC >= instructions_loaded_count && !halt_simulation) {
            // printf("Cycle %d: IF - No more instructions to fetch (PC=%d). Fetching NOP.\n", current_cycle, PC);
        } else if (PC > INSTRUCTION_MEM_END && !halt_simulation) {
            printf("Cycle %d: IF - PC (%d) out of instruction memory. Fetching NOP.\n", current_cycle, PC);
        }
        active_in_IF_stage.raw_instruction = (OPCODE_NOP << 28);
        active_in_IF_stage.instruction_pc_at_fetch = PC;
        active_in_IF_stage.valid = true;
        active_in_IF_stage.cycles_spent_in_stage = 0;
        active_in_IF_stage.decoded_info.original_pc = PC;
        active_in_IF_stage.decoded_info.opcode = OPCODE_NOP;
        active_in_IF_stage.decoded_info.type = 'N';
        printf("Cycle %d: IF - Inputs: PC=%d\n", current_cycle, PC);
        printf("Cycle %d: IF - Fetched NOP (0x%08X) for PC=%d.\n", current_cycle, active_in_IF_stage.raw_instruction, PC);
        printf("Cycle %d: IF - Outputs: RawInstr=0x%08X, NextPC=%d\n", current_cycle, active_in_IF_stage.raw_instruction, PC);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////// decode (el teneen) ///////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////


void decode_instruction_stage_op() {
    if (!active_in_ID_stage.valid) return;

    active_in_ID_stage.cycles_spent_in_stage++;
    DecodedInstruction* decoded = &active_in_ID_stage.decoded_info;

    if (active_in_ID_stage.cycles_spent_in_stage == 1) {
        decoded->opcode = (active_in_ID_stage.raw_instruction >> 28) & 0xF;
        decoded->original_pc = active_in_ID_stage.instruction_pc_at_fetch;
        printf("Cycle %d: ID - Inputs: RawInstr=0x%08X\n", current_cycle, active_in_ID_stage.raw_instruction);
        printf("Cycle %d: ID - Instr %d (0x%08X, %s) entered ID (1st cycle).\n",
               current_cycle, decoded->original_pc, active_in_ID_stage.raw_instruction, get_opcode_name(decoded->opcode));
        printf("Cycle %d: ID - Outputs: Opcode=%s\n", current_cycle, get_opcode_name(decoded->opcode));
    } else if (active_in_ID_stage.cycles_spent_in_stage == 2) {
        uint32_t raw_instr = active_in_ID_stage.raw_instruction;
        decoded->opcode = (raw_instr >> 28) & 0xF;
        decoded->original_pc = active_in_ID_stage.instruction_pc_at_fetch;
        printf("Cycle %d: ID - Inputs: RawInstr=0x%08X\n", current_cycle, raw_instr);

        // Check for load-use hazard (LW in EX)
        hazard_detected = false;
        if (active_in_EX_stage.valid && active_in_EX_stage.decoded_info.opcode == OPCODE_LW &&
            active_in_EX_stage.decoded_info.R1_idx != 0 &&
            (active_in_EX_stage.decoded_info.R1_idx == ((raw_instr >> 23) & 0x1F) ||
             active_in_EX_stage.decoded_info.R1_idx == ((raw_instr >> 18) & 0x1F) ||
             (decoded->opcode != OPCODE_SLL && decoded->opcode != OPCODE_SRL &&
              active_in_EX_stage.decoded_info.R1_idx == ((raw_instr >> 13) & 0x1F)))) {
            hazard_detected = true;
            printf("Cycle %d: ID - Load-use hazard detected on R%d. Stalling pipeline.\n",
                   current_cycle, active_in_EX_stage.decoded_info.R1_idx);
            active_in_ID_stage.cycles_spent_in_stage--; // Stay in ID cycle 2
            return;
        }

        switch (decoded->opcode) {
            case OPCODE_ADD: case OPCODE_SUB: case OPCODE_SLL: case OPCODE_SRL:
                decoded->type = 'R';
                decoded->R1_idx = (raw_instr >> 23) & 0x1F;
                decoded->R2_idx = (raw_instr >> 18) & 0x1F;
                if (decoded->opcode == OPCODE_SLL || decoded->opcode == OPCODE_SRL) {
                    decoded->shamt = raw_instr & 0x1FFF;
                    decoded->R3_idx = 0;
                } else {
                    decoded->R3_idx = (raw_instr >> 13) & 0x1F;
                    decoded->shamt = 0;
                }
                // Forwarding for R2
                decoded->val_R2_source = 0;
                if (decoded->R2_idx != 0) {
                    bool forwarded = false;
                    if (active_in_EX_stage.valid && active_in_EX_stage.cycles_spent_in_stage == 2 &&
                        active_in_EX_stage.decoded_info.R1_idx == decoded->R2_idx &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_BNE &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_J &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R2_source = active_in_EX_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from EX\n",
                               current_cycle, decoded->R2_idx, decoded->val_R2_source);
                    } else if (active_in_MEM_stage.valid &&
                               active_in_MEM_stage.decoded_info.R1_idx == decoded->R2_idx &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R2_source = (active_in_MEM_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_MEM_stage.decoded_info.mem_read_val :
                                                 active_in_MEM_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from MEM\n",
                               current_cycle, decoded->R2_idx, decoded->val_R2_source);
                    } else if (active_in_WB_stage.valid &&
                               active_in_WB_stage.decoded_info.R1_idx == decoded->R2_idx &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R2_source = (active_in_WB_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_WB_stage.decoded_info.mem_read_val :
                                                 active_in_WB_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from WB\n",
                               current_cycle, decoded->R2_idx, decoded->val_R2_source);
                    }
                    if (!forwarded) {
                        decoded->val_R2_source = registers[decoded->R2_idx];
                    }
                }
                // Forwarding for R3
                decoded->val_R3_source = 0;
                if (decoded->R3_idx != 0 && decoded->opcode != OPCODE_SLL && decoded->opcode != OPCODE_SRL) {
                    bool forwarded = false;
                    if (active_in_EX_stage.valid && active_in_EX_stage.cycles_spent_in_stage == 2 &&
                        active_in_EX_stage.decoded_info.R1_idx == decoded->R3_idx &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_BNE &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_J &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R3_source = active_in_EX_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from EX\n",
                               current_cycle, decoded->R3_idx, decoded->val_R3_source);
                    } else if (active_in_MEM_stage.valid &&
                               active_in_MEM_stage.decoded_info.R1_idx == decoded->R3_idx &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R3_source = (active_in_MEM_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_MEM_stage.decoded_info.mem_read_val :
                                                 active_in_MEM_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from MEM\n",
                               current_cycle, decoded->R3_idx, decoded->val_R3_source);
                    } else if (active_in_WB_stage.valid &&
                               active_in_WB_stage.decoded_info.R1_idx == decoded->R3_idx &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R3_source = (active_in_WB_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_WB_stage.decoded_info.mem_read_val :
                                                 active_in_WB_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from WB\n",
                               current_cycle, decoded->R3_idx, decoded->val_R3_source);
                    }
                    if (!forwarded) {
                        decoded->val_R3_source = registers[decoded->R3_idx];
                    }
                }
                break;

            case OPCODE_MULI: case OPCODE_ADDI: case OPCODE_BNE:
            case OPCODE_ANDI: case OPCODE_ORI: case OPCODE_LW: case OPCODE_SW:
                decoded->type = 'I';
                decoded->R1_idx = (raw_instr >> 23) & 0x1F;
                decoded->R2_idx = (raw_instr >> 18) & 0x1F;
                int32_t imm_val = raw_instr & 0x3FFFF;
                if (imm_val & (1 << 17)) {
                    imm_val |= ~0x3FFFF;
                }
                decoded->immediate = imm_val;
                // Forwarding for R1 (BNE, SW)
                decoded->val_R1_source = 0;
                if ((decoded->opcode == OPCODE_BNE || decoded->opcode == OPCODE_SW) && decoded->R1_idx != 0) {
                    bool forwarded = false;
                    if (active_in_EX_stage.valid && active_in_EX_stage.cycles_spent_in_stage == 2 &&
                        active_in_EX_stage.decoded_info.R1_idx == decoded->R1_idx &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_BNE &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_J &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R1_source = active_in_EX_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from EX\n",
                               current_cycle, decoded->R1_idx, decoded->val_R1_source);
                    } else if (active_in_MEM_stage.valid &&
                               active_in_MEM_stage.decoded_info.R1_idx == decoded->R1_idx &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R1_source = (active_in_MEM_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_MEM_stage.decoded_info.mem_read_val :
                                                 active_in_MEM_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from MEM\n",
                               current_cycle, decoded->R1_idx, decoded->val_R1_source);
                    } else if (active_in_WB_stage.valid &&
                               active_in_WB_stage.decoded_info.R1_idx == decoded->R1_idx &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R1_source = (active_in_WB_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_WB_stage.decoded_info.mem_read_val :
                                                 active_in_WB_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from WB\n",
                               current_cycle, decoded->R1_idx, decoded->val_R1_source);
                    }
                    if (!forwarded) {
                        decoded->val_R1_source = registers[decoded->R1_idx];
                    }
                }
                // Forwarding for R2
                decoded->val_R2_source = 0;
                if (decoded->R2_idx != 0) {
                    bool forwarded = false;
                    if (active_in_EX_stage.valid && active_in_EX_stage.cycles_spent_in_stage == 2 &&
                        active_in_EX_stage.decoded_info.R1_idx == decoded->R2_idx &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_BNE &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_J &&
                        active_in_EX_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R2_source = active_in_EX_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from EX\n",
                               current_cycle, decoded->R2_idx, decoded->val_R2_source);
                    } else if (active_in_MEM_stage.valid &&
                               active_in_MEM_stage.decoded_info.R1_idx == decoded->R2_idx &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_MEM_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R2_source = (active_in_MEM_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_MEM_stage.decoded_info.mem_read_val :
                                                 active_in_MEM_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from MEM\n",
                               current_cycle, decoded->R2_idx, decoded->val_R2_source);
                    } else if (active_in_WB_stage.valid &&
                               active_in_WB_stage.decoded_info.R1_idx == decoded->R2_idx &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_BNE &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_J &&
                               active_in_WB_stage.decoded_info.opcode != OPCODE_SW) {
                        decoded->val_R2_source = (active_in_WB_stage.decoded_info.opcode == OPCODE_LW) ?
                                                 active_in_WB_stage.decoded_info.mem_read_val :
                                                 active_in_WB_stage.decoded_info.alu_result;
                        forwarded = true;
                        printf("Cycle %d: ID - Forwarding R%d value %d from WB\n",
                               current_cycle, decoded->R2_idx, decoded->val_R2_source);
                    }
                    if (!forwarded) {
                        decoded->val_R2_source = registers[decoded->R2_idx];
                    }
                }
                break;

            case OPCODE_J:
                decoded->type = 'J';
                decoded->address = raw_instr & 0x0FFFFFFF;
                break;
            case OPCODE_NOP:
                decoded->type = 'N';
                break;
            default:
                printf("Cycle %d: ID - Instr %d - Unknown opcode 0x%X. Treating as NOP.\n",
                       current_cycle, decoded->original_pc, decoded->opcode);
                decoded->type = 'N';
                decoded->opcode = OPCODE_NOP;
                active_in_ID_stage.raw_instruction = (OPCODE_NOP << 28);
                break;
        }
        printf("Cycle %d: ID - Instr %d (%s) decoded (2nd cycle).\n", current_cycle, decoded->original_pc, get_opcode_name(decoded->opcode));
        printf("Cycle %d: ID - Outputs: Type=%c, R1_idx=%u, R2_idx=%u, R3_idx=%u, R1_val=%d, R2_val=%d, R3_val=%d, Imm=%d, Addr=%u, Shamt=%u\n",
               current_cycle, decoded->type, decoded->R1_idx, decoded->R2_idx, decoded->R3_idx,
               decoded->val_R1_source, decoded->val_R2_source, decoded->val_R3_source, decoded->immediate, decoded->address, decoded->shamt);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////// execute /////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void execute_instruction_stage_op() {
    if (!active_in_EX_stage.valid) return;
    if (active_in_EX_stage.decoded_info.type == 'N') {
        active_in_EX_stage.cycles_spent_in_stage++;
        return;
    }

    active_in_EX_stage.cycles_spent_in_stage++;
    DecodedInstruction* decoded = &active_in_EX_stage.decoded_info;
    int32_t pc_of_current_instruction = decoded->original_pc;

    if (active_in_EX_stage.cycles_spent_in_stage == 1) {
        printf("Cycle %d: EX - Inputs: Type=%c, R1_val=%d, R2_val=%d, R3_val=%d, Imm=%d, Addr=%u, Shamt=%u\n",
               current_cycle, decoded->type, decoded->val_R1_source, decoded->val_R2_source, decoded->val_R3_source,
               decoded->immediate, decoded->address, decoded->shamt);
        printf("Cycle %d: EX - Instr %d (%s) entered EX (1st cycle).\n",
               current_cycle, decoded->original_pc, get_opcode_name(decoded->opcode));
        printf("Cycle %d: EX - Outputs: None (1st cycle)\n", current_cycle);
    } else if (active_in_EX_stage.cycles_spent_in_stage == 2) {
        branch_taken_in_EX_cycle2 = false;
        switch (decoded->opcode) {
            case OPCODE_ADD:  decoded->alu_result = decoded->val_R2_source + decoded->val_R3_source; break;
            case OPCODE_SUB:  decoded->alu_result = decoded->val_R2_source - decoded->val_R3_source; break;
            case OPCODE_MULI: decoded->alu_result = decoded->val_R2_source * decoded->immediate;   break;
            case OPCODE_ADDI: decoded->alu_result = decoded->val_R2_source + decoded->immediate;   break;
            case OPCODE_BNE:
                if (decoded->val_R1_source != decoded->val_R2_source) {
                    branch_target_pc = pc_of_current_instruction + 1 + decoded->immediate;
                    branch_taken_in_EX_cycle2 = true;
                    decoded->alu_result = 1;
                } else {
                    decoded->alu_result = 0;
                }
                break;
            case OPCODE_ANDI: decoded->alu_result = decoded->val_R2_source & decoded->immediate;   break;
            case OPCODE_ORI:  decoded->alu_result = decoded->val_R2_source | decoded->immediate;   break;
            case OPCODE_J:
                {
                    uint32_t pc_plus_1 = (uint32_t)(pc_of_current_instruction + 1);
                    branch_target_pc = (pc_plus_1 & 0xF0000000) | (decoded->address & 0x0FFFFFFF);
                    branch_taken_in_EX_cycle2 = true;
                }
                break;
            case OPCODE_SLL:  decoded->alu_result = decoded->val_R2_source << decoded->shamt; break;
            case OPCODE_SRL:  decoded->alu_result = (int32_t)((uint32_t)decoded->val_R2_source >> decoded->shamt); break;
            case OPCODE_LW:
            case OPCODE_SW:   decoded->alu_result = decoded->val_R2_source + decoded->immediate; break;
            default: decoded->alu_result = 0; break;
        }
        printf("Cycle %d: EX - Instr %d (%s) executed (2nd cycle).\n", current_cycle, decoded->original_pc, get_opcode_name(decoded->opcode));
        printf("Cycle %d: EX - Outputs: ALU/Addr=%d, BranchTaken=%s\n",
               current_cycle, decoded->alu_result, branch_taken_in_EX_cycle2 ? "YES" : "NO");
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////// mem ///////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void memory_access_stage_op() {
    if (!can_MEM_operate_this_cycle) {
        printf("Cycle %d: MEM - Idle (IF active or waiting for branch resolution).\n", current_cycle);
        return;
    }
    if (!active_in_MEM_stage.valid) return;
    if (active_in_MEM_stage.decoded_info.type == 'N') {
        return;
    }

    active_in_MEM_stage.cycles_spent_in_stage = 1;
    DecodedInstruction* decoded = &active_in_MEM_stage.decoded_info;
    int32_t effective_address = decoded->alu_result;

    printf("Cycle %d: MEM - Inputs: ALU/Addr=%d, R1_val=%d\n", current_cycle, effective_address, decoded->val_R1_source);
    switch (decoded->opcode) {
        case OPCODE_LW:
            if (effective_address >= DATA_MEM_START && effective_address < MEMORY_SIZE) {
                decoded->mem_read_val = memory[effective_address];
                printf("Cycle %d: MEM - Instr %d (LW) from Addr %d. Read val: %d\n",
                       current_cycle, decoded->original_pc, effective_address, decoded->mem_read_val);
                printf("Cycle %d: MEM - Outputs: MemReadVal=%d\n", current_cycle, decoded->mem_read_val);
            } else {
                printf("Cycle %d: MEM - Instr %d (LW) - Error! Invalid mem read addr: %d. Reading 0.\n",
                       current_cycle, decoded->original_pc, effective_address);
                printf("Cycle %d: MEM - Outputs: MemReadVal=0\n", current_cycle);
                decoded->mem_read_val = 0;
            }
            break;
        case OPCODE_SW:
            if (effective_address >= DATA_MEM_START && effective_address < MEMORY_SIZE) {
                memory[effective_address] = decoded->val_R1_source;
                printf("Cycle %d: MEM - Instr %d (SW) to Addr %d. Wrote val: %d (from R%d)\n",
                       current_cycle, decoded->original_pc, effective_address, decoded->val_R1_source, decoded->R1_idx);
                printf("Cycle %d: MEM - Memory[0x%04X] changed to %d in MEM stage\n",
                       current_cycle, effective_address, decoded->val_R1_source);
                printf("Cycle %d: MEM - Outputs: None (write completed)\n", current_cycle);
            } else {
                printf("Cycle %d: MEM - Instr %d (SW) - Error! Invalid mem write addr: %d. Write ignored.\n",
                       current_cycle, decoded->original_pc, effective_address);
                printf("Cycle %d: MEM - Outputs: None (write ignored)\n", current_cycle);
            }
            break;
        default:
            printf("Cycle %d: MEM - Outputs: None (no memory operation)\n", current_cycle);
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////write back////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void write_back_stage_op() {
    if (!active_in_WB_stage.valid) return;
    if (active_in_WB_stage.decoded_info.type == 'N') {
        return;
    }

    active_in_WB_stage.cycles_spent_in_stage = 1;
    DecodedInstruction* decoded = &active_in_WB_stage.decoded_info;
    int32_t result_to_write = 0;
    bool perform_write = false;

    printf("Cycle %d: WB - Inputs: ALUResult=%d, MemReadVal=%d\n",
           current_cycle, decoded->alu_result, decoded->mem_read_val);
    switch (decoded->opcode) {
        case OPCODE_ADD: case OPCODE_SUB: case OPCODE_SLL: case OPCODE_SRL:
        case OPCODE_MULI: case OPCODE_ADDI: case OPCODE_ANDI: case OPCODE_ORI:
            result_to_write = decoded->alu_result;
            perform_write = true;
            break;
        case OPCODE_LW:
            result_to_write = decoded->mem_read_val;
            perform_write = true;
            break;
        case OPCODE_BNE: case OPCODE_J: case OPCODE_SW: case OPCODE_NOP:
            perform_write = false;
            break;
        default:
            printf("Cycle %d: WB - Instr %d (%s) - Error! Unknown opcode %u in WB. No write.\n",
                   current_cycle, decoded->original_pc, get_opcode_name(decoded->opcode), decoded->opcode);
            perform_write = false;
            break;
    }

    if (perform_write) {
        if (decoded->R1_idx != 0) {
            registers[decoded->R1_idx] = result_to_write;
            printf("Cycle %d: WB - Instr %d (%s) wrote %d to R%d.\n",
                   current_cycle, decoded->original_pc, get_opcode_name(decoded->opcode), result_to_write, decoded->R1_idx);
            printf("Cycle %d: WB - Register R%d changed to %d in WB stage\n",
                   current_cycle, decoded->R1_idx, result_to_write);
        } else {
            printf("Cycle %d: WB - Instr %d (%s) - Attempted write to R0 with value %d. Suppressed.\n",
                   current_cycle, decoded->original_pc, get_opcode_name(decoded->opcode), result_to_write);
            printf("Cycle %d: WB - Register R0 change to %d suppressed in WB stage\n",
                   current_cycle, result_to_write);
        }
        printf("Cycle %d: WB - Outputs: R%d=%d\n", current_cycle, decoded->R1_idx, result_to_write);
    } else {
        printf("Cycle %d: WB - Outputs: None (no write-back)\n", current_cycle);
    }
    registers[0] = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////// simulate process ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

void simulate_clock_cycle() {
    current_cycle++;
    printf("\n=============== Cycle %3d =============== (PC before fetch: %d)\n", current_cycle, PC);

    // Determine IF/MEM activity
    can_IF_operate_this_cycle = (current_cycle % 2 != 0); // Odd cycles for IF
    can_MEM_operate_this_cycle = (current_cycle % 2 == 0); // Even cycles for MEM

    if (stall_IF_for_mem_after_branch) {
        can_IF_operate_this_cycle = false;
        stall_IF_for_mem_after_branch = false;
        printf("Cycle %d: Control - IF stalled due to MEM access by prior branch/jump.\n", current_cycle);
    }

    // Track if branch is taken to suppress IF
    bool suppress_IF_this_cycle = false;

    // Print pipeline state at the start of the cycle in the requested format
    printf("--- Pipeline Stage Contents (Start of Cycle %d) ---\n", current_cycle);
    if (can_IF_operate_this_cycle && PC < MEMORY_SIZE) {
        uint32_t raw_instr = memory[PC];
        printf("IF (fetch buffer) : Instr PC %2d, Raw 0x%08X, Valid: %s, Opcode: %-4s\n",
               PC, raw_instr, "F", "---");
    } else {
        printf("IF (fetch buffer) : Instr PC %2d, Raw 0x%08X, Valid: %s, Opcode: %-4s\n",
               -1, 0, "F", "---");
    }
    printf("ID                : Instr PC %2d, Raw 0x%08X, Valid: %s, Opcode: %-4s, CycInStg: %d\n",
           active_in_ID_stage.valid ? active_in_ID_stage.instruction_pc_at_fetch : -1,
           active_in_ID_stage.raw_instruction,
           active_in_ID_stage.valid ? "T" : "F",
           active_in_ID_stage.valid ? get_opcode_name(active_in_ID_stage.decoded_info.opcode) : "---",
           active_in_ID_stage.cycles_spent_in_stage);
    printf("EX                : Instr PC %2d, Raw 0x%08X, Valid: %s, Opcode: %-4s, CycInStg: %d, ALU: %d\n",
           active_in_EX_stage.valid ? active_in_EX_stage.instruction_pc_at_fetch : -1,
           active_in_EX_stage.raw_instruction,
           active_in_EX_stage.valid ? "T" : "F",
           active_in_EX_stage.valid ? get_opcode_name(active_in_EX_stage.decoded_info.opcode) : "---",
           active_in_EX_stage.cycles_spent_in_stage,
           active_in_EX_stage.valid ? active_in_EX_stage.decoded_info.alu_result : 0);
    printf("MEM               : Instr PC %2d, Raw 0x%08X, Valid: %s, Opcode: %-4s, MemRead: %d\n",
           active_in_MEM_stage.valid ? active_in_MEM_stage.instruction_pc_at_fetch : -1,
           active_in_MEM_stage.raw_instruction,
           active_in_MEM_stage.valid ? "T" : "F",
           active_in_MEM_stage.valid ? get_opcode_name(active_in_MEM_stage.decoded_info.opcode) : "---",
           active_in_MEM_stage.valid ? active_in_MEM_stage.decoded_info.mem_read_val : 0);
    printf("WB                : Instr PC %2d, Raw 0x%08X, Valid: %s, Opcode: %-4s\n",
           active_in_WB_stage.valid ? active_in_WB_stage.instruction_pc_at_fetch : -1,
           active_in_WB_stage.raw_instruction,
           active_in_WB_stage.valid ? "T" : "F",
           active_in_WB_stage.valid ? get_opcode_name(active_in_WB_stage.decoded_info.opcode) : "---");
    printf("-----------------------------------------------------------------------\n");

    // Process stages in reverse order
    write_back_stage_op();
    if (can_MEM_operate_this_cycle) memory_access_stage_op();
    execute_instruction_stage_op();

    // Handle control hazards immediately after EX stage
    if (branch_taken_in_EX_cycle2) {
        printf("Cycle %d: Control - Branch/Jump taken in EX to PC 0x%X. Flushing ID & IF contents.\n",
               current_cycle, branch_target_pc);
        PC = branch_target_pc;
        active_in_ID_stage.valid = false;
        memset(&active_in_ID_stage.decoded_info, 0, sizeof(DecodedInstruction));
        active_in_ID_stage.decoded_info.type = 'N';
        active_in_IF_stage.valid = false;
        memset(&active_in_IF_stage.decoded_info, 0, sizeof(DecodedInstruction));
        active_in_IF_stage.decoded_info.type = 'N';
        suppress_IF_this_cycle = true; // Prevent IF from fetching this cycle
        if (current_cycle % 2 != 0) {
            stall_IF_for_mem_after_branch = true;
            printf("Cycle %d: Control - Scheduling IF stall for next cycle (Cycle %d) due to branch.\n", current_cycle, current_cycle + 1);
        }
        branch_taken_in_EX_cycle2 = false;
    }

    // Process remaining stages after flush
    decode_instruction_stage_op();
    if (hazard_detected) {
        can_IF_operate_this_cycle = false;
        active_in_EX_stage.valid = false; // Insert NOP
        printf("Cycle %d: Control - Pipeline stalled for load-use hazard.\n", current_cycle);
    } else if (can_IF_operate_this_cycle && !suppress_IF_this_cycle) {
        fetch_instruction_stage_op();
    } else if (suppress_IF_this_cycle) {
        printf("Cycle %d: IF - Suppressed due to branch taken in EX.\n", current_cycle);
        active_in_IF_stage.valid = false; // Ensure IF remains invalid
        memset(&active_in_IF_stage.decoded_info, 0, sizeof(DecodedInstruction));
        active_in_IF_stage.decoded_info.type = 'N';
    }

    // Latching
    if (active_in_MEM_stage.valid && can_MEM_operate_this_cycle) {
        active_in_WB_stage = active_in_MEM_stage;
        active_in_WB_stage.cycles_spent_in_stage = 0;
    } else {
        active_in_WB_stage.valid = false;
    }

    if (active_in_EX_stage.valid && active_in_EX_stage.cycles_spent_in_stage == 2) {
        active_in_MEM_stage = active_in_EX_stage;
        active_in_MEM_stage.cycles_spent_in_stage = 0;
    } else {
        active_in_MEM_stage.valid = false;
    }

    if (active_in_ID_stage.valid && active_in_ID_stage.cycles_spent_in_stage == 2 && !hazard_detected) {
        active_in_EX_stage = active_in_ID_stage;
        active_in_EX_stage.cycles_spent_in_stage = 0;
    } else if (!(active_in_EX_stage.valid && active_in_EX_stage.cycles_spent_in_stage == 1)) {
        active_in_EX_stage.valid = false;
    }

    if (active_in_IF_stage.valid && can_IF_operate_this_cycle && !suppress_IF_this_cycle && !hazard_detected) {
        active_in_ID_stage = active_in_IF_stage;
        active_in_ID_stage.cycles_spent_in_stage = 0;
    } else if (!(active_in_ID_stage.valid && active_in_ID_stage.cycles_spent_in_stage == 1)) {
        active_in_ID_stage.valid = false;
    }

    // Halt conditions
    static int empty_pipeline_cycles = 0;
    if (PC >= instructions_loaded_count && !active_in_IF_stage.valid && !active_in_ID_stage.valid &&
        !active_in_EX_stage.valid && !active_in_MEM_stage.valid && !active_in_WB_stage.valid) {
        empty_pipeline_cycles++;
        if (empty_pipeline_cycles > 2) {
            halt_simulation = 1;
            printf("\nHALT: PC (%d) >= Instructions Loaded (%d) and pipeline fully empty for %d cycles.\n", PC, instructions_loaded_count, empty_pipeline_cycles);
        }
    } else {
        empty_pipeline_cycles = 0;
    }

    if (current_cycle > instructions_loaded_count + 30 && instructions_loaded_count > 0) {
        printf("\nHALT: Cycle limit safety break (%d cycles for %d instructions).\n", current_cycle, instructions_loaded_count);
        halt_simulation = 1;
    }
    if (instructions_loaded_count == 0 && current_cycle > 10) {
        printf("\nHALT: No program loaded after 10 cycles.\n");
        halt_simulation = 1;
    }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////main///////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

// --- Main Simulation Loop ---
int main(int argc, char* argv[]) {
    if (argc == 2) {
        initialize_processor();
        load_assembly_file(argv[1]);
    }
    printf("\n--- Starting Simulation (Package 1 Logic) ---\n");
    while (!halt_simulation) {
        simulate_clock_cycle();
    }
    printf("\n--- Simulation Ended after %d cycles ---\n", current_cycle);
    printf("Final Registers (including special purpose):\n");
    printf("PC: %10d (0x%08X)\n", PC, (unsigned int)PC);
    for (int i = 0; i < NUM_REGISTERS; i++) {
        printf("R%02d: %10d (0x%08X)", i, registers[i], (unsigned int)registers[i]);
        if ((i + 1) % 4 == 0) printf("\n"); else printf("  |  ");
    }
    if (NUM_REGISTERS % 4 != 0) printf("\n");
    printf("\nFinal Instruction Memory (0 to %d):\n", INSTRUCTION_MEM_END);
    for (int i = 0; i <= INSTRUCTION_MEM_END && i < MEMORY_SIZE; i++) {
        printf("Mem[%04d]: 0x%08X (%s)\n", i, memory[i], get_opcode_name((memory[i] >> 28) & 0xF));
    }
    printf("\nFinal Data Memory (%d to %d):\n", DATA_MEM_START, MEMORY_SIZE - 1);
    for (int i = DATA_MEM_START; i < MEMORY_SIZE; i++) {
        printf("Mem[%04d]: %10d (0x%08X)\n", i, (int32_t)memory[i], memory[i]);
    }
    return 0;
}
