#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define LINESIZE 50
#define _DIS 0
#define _SIM 1

int _mode;            // Dis | Sim
FILE *in, *out;            // I/O Files
char line[LINESIZE] = {0};    // Line Buffer
char trace[10];            // [Tx:x] operation
int memLocation = 496;        // Starting Location
bool endProgram = 0;        // Determines End
bool ending = 0;        // Determines Ending
bool flush = 0;            // Flush Pipeline Boolean

//Breaking Down Instruction
void storeInstruction();
void buildInstruction();
void getHexString();
int  bin2Dec(char name[], int length);

//Modes
void disassembly();
void simulation();

//Print Cycle
void printCycle();

//Pipeline Stages
void _writeBack();
void _dataFetch2();
void _dataFetch1();
void _execute();
void _registerFetch();
void _instructionDecode();
void _instructionFetch2();
void _instructionFetch1();

//Pipeline Operations
void resetForwardings();

//Instruction Structure
struct Instruction{
    char binString[33];
    char binStringX[38];
    char opcode[7];
    char rs[6];
    char rt[6];
    char ra[6];
    char rd[6];
    char sa[6];
    char function[7];
    char immediate[17];
    char target[27];
    char iText[6];
    char assemblyString[20];
    char hexString[12];
    int type; // 0-R, 1-J, 2-I
    int irs;
    int irt;
    int ira;
    int ird;
    int isa;
    signed int sImm;
    unsigned int uImm;
    int iTarget;
    int memoryLocation;
    bool NOP;
    bool STALL;
    bool branchPrediction;
    bool branchActual;
    Instruction *next;
}instruction, *start, *curr, *IF, *IS, *ID, *RF, *EX, *DF, *DS, *WB, *stall;

struct PipelineRegister{
    int IF_IS_NPC;
    char IS_ID_IR[15];
    int ID_RF;
    int RF_EX_A;
    int RF_EX_B;
    int EX_DF_ALUout;
    int EX_DF_B;
    int DF_DS;
    int DS_WB;
    int DS_WB_ALUoutLMD;
}pipelineRegister;

struct Pipeline{
    int cycle;
    int currPC;
    int newPC;
    int dataMemLocation;
    bool stall;
    int flush;
}pipeline;

struct Stalls{
    int loads;
    int branches;
    int other;
}stalls;

struct Forwardings{
    int ds[2];
    int df[2];
    int ex[2];
   
    int exdf_rfex;
    int dfds_exdf;
    int dfds_rfex;
    int dswb_exdf;
    int dswb_rfex;
}forwardings;

struct Scoreboard{
    bool exdf_rfex;
    bool dfds_exdf;
    bool dfds_rfex;
    bool dswb_exdf;
    bool dswb_rfex;
    char exdf_rfexString[50];
    char dfds_exdfString[50];
    char dfds_rfexString[50];
    char dswb_exdfString[50];
    char dswb_rfexString[50];
}scoreboard;

int integerRegister[32] = {0};
int dataMemory[10]      = {0};

int main(int argc, const char* argv[]){

    //Open files
    in =  fopen(argv[1], "r");
    out = fopen(argv[2], "w");

    //Determine mode
    if (!strcmp(argv[3], "dis"))
        _mode = 0;
    else if (!strcmp(argv[3], "sim"))
        _mode = 1;
    else
        _mode = 2;

    //Initialize first node in linked list
    start = (Instruction *)malloc(sizeof(Instruction));
    curr = start;
    curr->next = NULL;

    stall = (Instruction *)malloc(sizeof(Instruction));
    stall->STALL = true;

    //Get All Instructions
    while (fgets(line, LINESIZE, in) != NULL){

        //Edit memory variables
        curr->memoryLocation = memLocation;
        memLocation += 4;

        //Compile instruction
        storeInstruction();
        buildInstruction();

        //Determine Data Location Start
        if (!strcmp(curr->iText, "BREAK")){
            if (curr->memoryLocation < 600)
                pipeline.dataMemLocation = 600;
            else
                pipeline.dataMemLocation = curr->memoryLocation + 4;
        }
       
        //Allocate space for next instruction
        curr->next = (Instruction *)malloc(sizeof(Instruction));
        curr = curr->next;
    }


    //Close Input
    fclose(in);

    // Determine Operation
    if (_mode == _DIS)
        disassembly();
    else if (_mode == _SIM)
        simulation();
    else
        printf("Invalid Operation\n");

    //Close Output
    fclose(out);
   
    if (_mode != 2)
        printf("Finished %s %s to %s \n", (_mode == 0) ? "disassembling" : "simulating", argv[1],  argv[2]);

    return 0;
}

void disassembly(){
    curr = start;
    while (curr->next != NULL){
        fprintf(out, "%s\t%d\t%s\n", (endProgram) ? curr->binString : curr->binStringX, curr->memoryLocation,
                         (endProgram) ? "" : curr->assemblyString);
        if (!strcmp(curr->iText,"BREAK"))
            endProgram = true;
        curr = curr->next;
    }
}

void resetForwardings(){
    forwardings.ex[0] = forwardings.ex[1] =
    forwardings.ds[0] = forwardings.ds[1] =
    forwardings.df[0] = forwardings.df[1] =
    -1;

}

void simulation(){

    //Assume Entry Point [496]
    curr = start;
    pipeline.cycle = 0;
    pipeline.flush = -1;
    pipeline.currPC = curr->memoryLocation;

    //Initialize Register Pointers
    IF = IS = ID = RF = EX = DF = DS = WB = (Instruction *)malloc(sizeof(Instruction));

    //Initialize all stages to NOP
    IF->NOP   = IS->NOP   = ID->NOP   = RF->NOP   = EX->NOP   = DF->NOP   = DS->NOP   = WB->NOP   = true;
   
    //PIPELINE
    while(1){
        _writeBack();
        _dataFetch2();
        _dataFetch1();
        _execute();
        _registerFetch();
        _instructionDecode();
        _instructionFetch2();
        _instructionFetch1();


        pipeline.stall = false;
        printCycle();
        if (endProgram) break;
        pipeline.cycle++;
        if (pipeline.cycle > 100) break;

        resetForwardings();
        if (pipeline.flush > 0){
            pipeline.currPC = pipeline.flush;
            pipeline.flush = -1;
            IF = IS = ID = RF = stall;
        }
    }

}

void _writeBack(){

    WB = DS;

        if (WB == stall){
            return;
        }

        if (!strcmp(WB->iText,"BREAK")){
            endProgram = 1;
            return;
        }

        if (!strcmp(WB->iText, "ADDI" ) ||
            !strcmp(WB->iText, "ADDIU") ||
            !strcmp(WB->iText, "SLTI" )){
            //Assign Register Value
            integerRegister[WB->irt] = pipelineRegister.DS_WB;
            return;
        }

        if (!strcmp(WB->iText, "ADD" ) ||
            !strcmp(WB->iText, "ADDU") ||
            !strcmp(WB->iText, "AND" ) ||
            !strcmp(WB->iText, "OR"  ) ||
            !strcmp(WB->iText, "XOR" ) ||
            !strcmp(WB->iText, "NOR" ) ||
            !strcmp(WB->iText, "SUB" ) ||
            !strcmp(WB->iText, "SUBU") ||
            !strcmp(WB->iText, "SLT" ) ||
            !strcmp(WB->iText, "SLL" ) ||
            !strcmp(WB->iText, "SRL" ) ||
            !strcmp(WB->iText, "SRA" )){
            //Assign Register Value
            integerRegister[WB->ird] = pipelineRegister.DS_WB;
            return;
        }

        if (!strcmp(WB->iText, "LW")){
            //Finish Loading Register -> Memory
            integerRegister[WB->irt] = dataMemory[(WB->sImm + WB->irs - pipeline.dataMemLocation)/4];
            return;
        }
       
        if (!strcmp(WB->iText, "SW"  ) ||
            !strcmp(WB->iText, "BEQ" ) ||
            !strcmp(WB->iText, "BNE" ) ||
            !strcmp(WB->iText, "BGTZ") ||
            !strcmp(WB->iText, "BLTZ") ||
            !strcmp(WB->iText, "BLEZ") ||
            !strcmp(WB->iText, "BGEZ") ||
            !strcmp(WB->iText, "J"   ) ||
            !strcmp(WB->iText, "JR"  )){
            //Nothing Occurs in WB for these
            return;
        }

}

void _dataFetch2(){

    DS = DF;

        if (DS == stall){
            forwardings.ds[0] = -1;
            forwardings.ds[1] = -1;
            return;
        }

        if (!strcmp(DS->iText, "BREAK")){
            forwardings.ds[0] = -1;
            forwardings.ds[1] = -1;   
            return;
        }

        if (!strcmp(DF->iText, "ADDIU")||
            !strcmp(DF->iText, "ADDI") ||
            !strcmp(DF->iText, "SLTI")){
            pipelineRegister.DS_WB = pipelineRegister.DF_DS;
            forwardings.ds[0] = DS->irt;
            forwardings.ds[1] = pipelineRegister.DF_DS;
            return;
        }

        if (!strcmp(DF->iText, "ADDU") ||
            !strcmp(DF->iText, "ADD" ) ||
            !strcmp(DF->iText, "SUB" ) ||
            !strcmp(DF->iText, "SUBU") ||
            !strcmp(DF->iText, "AND" ) ||
            !strcmp(DF->iText, "OR"  ) ||
            !strcmp(DF->iText, "XOR" ) ||
            !strcmp(DF->iText, "NOR" ) ||
            !strcmp(DF->iText, "SRA" ) ||
            !strcmp(DF->iText, "SRL" ) ||
            !strcmp(DF->iText, "SLL" ) ||
            !strcmp(DF->iText, "SLT" )){
            //Nothing Occurs, Pass Along
            pipelineRegister.DS_WB = pipelineRegister.DF_DS;
            forwardings.ds[0] = DS->ird;
            forwardings.ds[1] = pipelineRegister.DF_DS;
            return;
        }
       
        if (!strcmp(DS->iText, "SW")){
            dataMemory[(DS->sImm + DS->irs - pipeline.dataMemLocation)/4] = integerRegister[DS->irt];
            pipelineRegister.DS_WB = pipelineRegister.DF_DS;
            forwardings.ds[0] = -1;
            forwardings.ds[1] = -1;   
            return;   
        }

        if (!strcmp(DS->iText, "LW")){
            pipelineRegister.DS_WB = dataMemory[pipelineRegister.DF_DS];
            forwardings.ds[0] = DS->irt;
            forwardings.ds[1] = pipelineRegister.DF_DS;
            return;
        }

        if (!strcmp(DS->iText, "BEQ" ) ||
            !strcmp(DS->iText, "BNE" ) ||
            !strcmp(DS->iText, "BGTZ") ||
            !strcmp(DS->iText, "BLTZ") ||
            !strcmp(DS->iText, "BGEZ") ||
            !strcmp(DS->iText, "BLEZ") ||
            !strcmp(DS->iText, "J"   ) ||
            !strcmp(DS->iText, "JR"  )){
            //Nothing Occurs, Pass Along
            pipelineRegister.DS_WB = pipelineRegister.DF_DS;
            forwardings.ds[0] = -1;
            forwardings.ds[0] = -1;
            return;
        }

}

void _dataFetch1(){

    DF = EX;

        if (DF == stall){
            forwardings.df[0] = -1;
            forwardings.df[1] = -1;   
            return;
        }

        if (!strcmp(DF->iText, "BREAK")){
            forwardings.df[0] = -1;
            forwardings.df[1] = -1;   
            return;
        }

        if (!strcmp(DF->iText, "ADDIU")||
            !strcmp(DF->iText, "ADDI") ||
            !strcmp(DF->iText, "SLTI")){
            pipelineRegister.DF_DS = pipelineRegister.EX_DF_ALUout;
            forwardings.df[0] = DF->irt;
            forwardings.df[1] = pipelineRegister.EX_DF_ALUout;
            return;
           
        }

        if(!strcmp(DF->iText, "ADDU") ||
            !strcmp(DF->iText, "ADD" ) ||
            !strcmp(DF->iText, "SUB" ) ||
            !strcmp(DF->iText, "SUBU") ||
            !strcmp(DF->iText, "AND" ) ||
            !strcmp(DF->iText, "OR"  ) ||
            !strcmp(DF->iText, "XOR" ) ||
            !strcmp(DF->iText, "NOR" ) ||
            !strcmp(DF->iText, "SRA" ) ||
            !strcmp(DF->iText, "SRL" ) ||
            !strcmp(DF->iText, "SLL" ) ||
            !strcmp(DF->iText, "SLT" )){
            pipelineRegister.DF_DS = pipelineRegister.EX_DF_ALUout;
            forwardings.df[0] = DF->ird;
            forwardings.df[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }

        if (!strcmp(DF->iText, "SW")){
            pipelineRegister.DF_DS = pipelineRegister.EX_DF_ALUout;
            forwardings.df[0] = -1;
            forwardings.df[1] = -1;
            return;
        }

        if (!strcmp(DF->iText, "LW")){
            pipelineRegister.DF_DS = pipelineRegister.EX_DF_ALUout; //dataMemory[(pipelineRegister.EX_DF_ALUout - pipeline.dataMemLocation)/4];
            forwardings.df[0] = DF->irt;
            forwardings.df[1] = -1;// dataMemory[(pipelineRegister.EX_DF_ALUout - pipeline.dataMemLocation)/4];
            return;
           
        }

        if (!strcmp(DF->iText, "BEQ" ) ||
            !strcmp(DF->iText, "BNE" ) ||
            !strcmp(DF->iText, "BGTZ") ||
            !strcmp(DF->iText, "BLTZ") ||
            !strcmp(DF->iText, "BGEZ") ||
            !strcmp(DF->iText, "BLEZ")){
            forwardings.df[0] = -1;
            forwardings.df[1] = -1;
            return;
        }
        //J, JR, BREAK
   
}

void _execute(){

    EX = RF;


    if (EX == stall){
        forwardings.ex[0] = -1;
        forwardings.ex[1] = -1;
        return;
    }


    /*
   
        FORWARDINGS OCCUR HERE!

    */

    int RSvalue = 0;
    int RTvalue = 0;
    int RDvalue = 0;


    if (EX->type == 0){ //R
        if ((forwardings.df[0] == EX->irs) && (forwardings.df[1] != -1)){
            RSvalue = forwardings.df[1];
        }
        else if ((forwardings.ds[0] == EX->irs) && (forwardings.ds[1] != -1)){
            RSvalue = forwardings.ds[1];
        }
        else if ((forwardings.df[0] == EX->irs) && (forwardings.df[1] != -1)){
            RSvalue = forwardings.df[1];
        }
       
       






        else if ((forwardings.df[0] == EX->irs) && (forwardings.df[1] == -1)){
            EX = stall;
            pipeline.stall = true;
        }
        else if ((forwardings.df[0] == EX->irs) && (forwardings.df[1] == -1)){
            EX = stall;
            pipeline.stall = true;
        }
       
        if ((forwardings.df[0] == EX->irt) && (forwardings.df[1] != -1)){
            RTvalue = forwardings.df[1];
        }
    }

    if (EX->type == 1){ //J
        //Nothing to do with forwarding here
    }

    if (EX->type == 2){ //I
       
    }


        if (!strcmp(EX->iText, "BREAK")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            ending = true;
            IF->NOP = IS->NOP = ID->NOP = false;
            return;
        }

        if (!strcmp(EX->iText, "ADDI")){
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] + EX->sImm;
            forwardings.ex[0] = EX->irt;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }

        if (!strcmp(EX->iText, "ADDIU")){
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] + EX->sImm;
            forwardings.ex[0] = EX->irt;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }

        if (!strcmp(EX->iText, "ADD")){
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] + integerRegister[EX->irt];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "AND")){
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] & integerRegister[EX->irt];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "OR")){
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] | integerRegister[EX->irt];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "XOR")){
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] ^ integerRegister[EX->irt];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "NOR")){
            pipelineRegister.EX_DF_ALUout = ~(integerRegister[EX->irs] | integerRegister[EX->irt]);
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "SLL")){//Fill 0s
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irt] << integerRegister[EX->isa];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "SRL")){//Fill 0s
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irt] >> integerRegister[EX->isa];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "SRA")){//Fill 1s
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irt] >> integerRegister[EX->isa];
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "SLT")){
            pipelineRegister.EX_DF_ALUout = (integerRegister[EX->irs] < integerRegister[EX->irt]) ? 1 : 0;
            forwardings.ex[0] = EX->ird;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "SLTI")){
            pipelineRegister.EX_DF_ALUout = (integerRegister[EX->irs] < EX->sImm) ? 1 : 0;
            forwardings.ex[0] = EX->irt;
            forwardings.ex[1] = pipelineRegister.EX_DF_ALUout;
            return;
        }
        if (!strcmp(EX->iText, "BEQ")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = pipeline.currPC;
            (integerRegister[EX->irs] == integerRegister[EX->irt]) ? pipelineRegister.EX_DF_ALUout = pipeline.currPC + EX->sImm*4 : 0;
            (integerRegister[EX->irs] == integerRegister[EX->irt]) ? EX->branchActual = true : EX->branchActual = false;
            if (EX->branchActual == true){
                pipeline.flush = EX->sImm*4 + pipeline.currPC - 12; //12 Comes from Delay in Pipeline
            }

            return;
        }
        if (!strcmp(EX->iText, "BNE")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = pipeline.currPC;
            (integerRegister[EX->irs] != integerRegister[EX->irt]) ? pipelineRegister.EX_DF_ALUout = pipeline.currPC + EX->sImm*4 : 0;
            (integerRegister[EX->irs] != integerRegister[EX->irt]) ? EX->branchActual = true : EX->branchActual = false;
            if (EX->branchActual == true){
                pipeline.flush = EX->sImm*4 + pipeline.currPC - 12; //12 Comes from Delay in Pipeline
            }

            return;
        }
        if (!strcmp(EX->iText, "BGEZ")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = pipeline.currPC;
            (integerRegister[EX->irs] >= 0) ? pipelineRegister.EX_DF_ALUout = pipeline.currPC + EX->sImm*4 : 0;
            (integerRegister[EX->irs] >= 0) ? EX->branchActual = true : EX->branchActual = false;
            if (EX->branchActual == true){
                pipeline.flush = EX->sImm*4 + pipeline.currPC - 12; //12 Comes from Delay in Pipeline
            }
            return;
        }
        if (!strcmp(EX->iText, "BGTZ")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = pipeline.currPC;
            (integerRegister[EX->irs] >  0) ? pipelineRegister.EX_DF_ALUout = pipeline.currPC + EX->sImm*4 : 0;
            (integerRegister[EX->irs] >  0) ? EX->branchActual = true : EX->branchActual = false;
            if (EX->branchActual == true){
                pipeline.flush = EX->sImm*4 + pipeline.currPC - 12; //12 Comes from Delay in Pipeline
            }
            return;
        }
        if (!strcmp(EX->iText, "BLEZ")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = pipeline.currPC;
            (integerRegister[EX->irs] <= 0) ? pipelineRegister.EX_DF_ALUout = pipeline.currPC + EX->sImm*4 : 0;
            (integerRegister[EX->irs] <= 0) ? EX->branchActual = true : EX->branchActual = false;
            if (EX->branchActual == true){
                pipeline.flush = EX->sImm*4 + pipeline.currPC - 12; //12 Comes from Delay in Pipeline
            }
            return;
        }
        if (!strcmp(EX->iText, "BLTZ")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = pipeline.currPC;
            (integerRegister[EX->irs] <  0) ? pipelineRegister.EX_DF_ALUout = pipeline.currPC + EX->sImm*4 : 0;
            (integerRegister[EX->irs] <  0) ? EX->branchActual = true : EX->branchActual = false;
            if (EX->branchActual == true){
                pipeline.flush = EX->sImm*4 + pipeline.currPC - 12; //12 Comes from Delay in Pipeline
            }
            return;
        }
        if (!strcmp(EX->iText, "SW")){//RS - base
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] + EX->sImm;
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            return;
        }
        if (!strcmp(EX->iText, "LW")){//RS - base
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs] + EX->sImm;
            forwardings.ex[0] = EX->irt;
            forwardings.ex[1] = -1;
            return;
        }
       
        if (!strcmp(EX->iText, "J")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = EX->iTarget*4;
            pipeline.flush = pipelineRegister.EX_DF_ALUout;
            return;
        }

        if (!strcmp(EX->iText, "JR")){
            forwardings.ex[0] = -1;
            forwardings.ex[1] = -1;
            pipelineRegister.EX_DF_ALUout = integerRegister[EX->irs]*4;
            pipeline.currPC = integerRegister[EX->irs]*4;
            return;
        }

}

void _registerFetch(){

    //Is ID ready
    if (!strcmp(ID->iText, "ADDI" ) ||
        !strcmp(ID->iText, "ADDIU") ||
        !strcmp(ID->iText, "SLTI" )){
        if (((ID->irs == forwardings.ex[0]) && (forwardings.ex[1] == -1)) ||
            ((ID->irs == forwardings.ds[0]) && (forwardings.ds[1] == -1)) ||
            ((ID->irs == forwardings.df[0]) && (forwardings.df[1] == -1))){
            RF = stall;
            pipeline.stall = true;
            return;
        }
    }
   
    if (!strcmp(ID->iText, "ADD" ) ||
        !strcmp(ID->iText, "ADDU") ||
        !strcmp(ID->iText, "AND" ) ||
        !strcmp(ID->iText, "OR"  ) ||
        !strcmp(ID->iText, "XOR" ) ||
        !strcmp(ID->iText, "NOR" ) ||
        !strcmp(ID->iText, "SUB" ) ||
        !strcmp(ID->iText, "SUBU") ||
        !strcmp(ID->iText, "SLT" ) ||
        !strcmp(ID->iText, "SLL" ) ||
        !strcmp(ID->iText, "SRL" ) ||
        !strcmp(ID->iText, "SRA" )){
       
        if (((ID->irs == forwardings.ex[0]) && (forwardings.ex[1] == -1)) ||
            ((ID->irs == forwardings.ds[0]) && (forwardings.ds[1] == -1)) ||
            ((ID->irs == forwardings.df[0]) && (forwardings.df[1] == -1)) ||
            ((ID->irt == forwardings.ex[0]) && (forwardings.ex[1] == -1)) ||
            ((ID->irt == forwardings.ds[0]) && (forwardings.ds[1] == -1)) ||
            ((ID->irt == forwardings.df[0]) && (forwardings.df[1] == -1))){
            RF = stall;
            pipeline.stall = true;
            return;
        }
       
    }
   
    RF = ID;

    if (RF == stall){
        return;
    }

}

void _instructionDecode(){

    if (pipeline.stall == true){
        return;
    }

    ID = IS;

    if (ID == stall){
        return;
    }
   
    //DECODE;
}

void _instructionFetch2(){

    //Pass Items on
    //Fetches hexString
   
    if (pipeline.stall == true){
        return;
    }

    IS = IF;

    if (IS == stall){
        return;
    }

    strcpy(pipelineRegister.IS_ID_IR, IS->hexString);
   
}

void _instructionFetch1(){

    if (pipeline.stall == true){
        return;
    }

    curr = start;

    while (curr->memoryLocation != pipeline.currPC)
        curr = curr->next;

    IF = curr;

    //calculate newPC
    pipeline.currPC += 4;
    pipelineRegister.IF_IS_NPC = pipeline.currPC;

}

void printCycle(){
    char sTemp[] = "-";

    //Cycle & Current PC
    fprintf(out, "***** Cycle #%d***********************************************\n"
        "Current PC = %d:\n\n",
        pipeline.cycle, IF->memoryLocation);

    //Pipeline Status
    fprintf(out, "Pipeline Status:\n"
        "* IF : <unknown> \n"
        "* IS : Fetched <%s>\n"
        "* ID : %s \n"
        "* RF : %s \n"
        "* EX : %s \n"
        "* DF : %s \n"
        "* DS : %s \n"
        "* WB : %s \n\n",
        (IS->NOP) ? "NOP" : (IS == stall) ? "** STALL**" : IS->hexString,
        (ID->NOP) ? "NOP" : (ID == stall) ? "** STALL**" : ID->assemblyString,
        (RF->NOP) ? "NOP" : (RF == stall) ? "** STALL**" : RF->assemblyString,
        (EX->NOP) ? "NOP" : (EX == stall) ? "** STALL**" : EX->assemblyString,
        (DF->NOP) ? "NOP" : (DF == stall) ? "** STALL**" : DF->assemblyString,
        (DS->NOP) ? "NOP" : (DS == stall) ? "** STALL**" : DS->assemblyString,
        (WB->NOP) ? "NOP" : (WB == stall) ? "** STALL**" : WB->assemblyString);
   
    //Stall instruction
    fprintf(out, "Stall Instruction: %s \n\n",
        (RF == stall) ? ID->assemblyString : "(none)");
    /*
    //Forwarding
    fprintf(out, "Forwarding:\n"
        "-Detected: %s \n"
        "-Forwarded:\n"
        " * EX/DF -> RF/EX : %d \n"
        " * DF/DS -> EX/DF : %d \n"
        " * DF/DS -> RF/EX : %d \n"
        " * DS/WB -> EX/DF : %d \n"
        " * DS/WB -> RF/EX : %d \n\n",
        sTemp,                 forwardings.exdf_rfex, forwardings.dfds_exdf,
        forwardings.dfds_rfex, forwardings.dswb_exdf, forwardings.dswb_rfex);
    */
    //Pipeline Registers
    fprintf(out, "Pipeline Registers:\n"
        "* IF/IS.NPC\t: %d\n"
        "* IS/ID.IR\t: %s\n"
        "* RF/EX.A\t: %d\n"
        "* RF/EX.B\t: %d\n"
        "* EX/DF.ALUout\t: %d\n"
        "* EX/DF.B\t: %d\n"
        "* DS/WB.ALUoutLMD\t: %d\n\n",
        pipelineRegister.IF_IS_NPC,
        (pipelineRegister.IS_ID_IR[1] != 0) ? pipelineRegister.IS_ID_IR : "0",
        pipelineRegister.RF_EX_A,
        pipelineRegister.RF_EX_B,
        pipelineRegister.EX_DF_ALUout,
        pipelineRegister.EX_DF_B,
        pipelineRegister.DS_WB_ALUoutLMD);

   
    fprintf(out, "Integer registers:\n");
        int u;
        for (u = 0; u < 32; u++){
            fprintf(out, "R%d\t%d",u,integerRegister[u]);
            ((u+1)%4==0) ? fprintf(out,"\n") : fprintf(out,"\t"); //print \n every 4
        }
   
    //Data Memory
    fprintf(out, "\nData Memory:\n");
        for (u = 0; u < 10; u++){
            fprintf(out, "%3d: %d\n", pipeline.dataMemLocation + u*4 , dataMemory[u]);
        }   

   
    //Total Stalls
    fprintf(out, "\nTotal Stalls:\n"
        "*Loads     : %d\n"
        "*Branches  : %d\n"
        "*Other     : %d\n\n",
        stalls.loads, stalls.branches, stalls.other);

    //Total Forwardings
    fprintf(out, "Total Forwardings:\n"
        "* EX/DF -> RF/EX : %d \n"
        "* DF/DS -> EX/DF : %d \n"
        "* DF/DS -> RF/EX : %d \n"
        "* DS/WB -> EX/DF : %d \n"
        "* DS/WB -> RF/EX : %d \n\n",
        forwardings.exdf_rfex,
        forwardings.dfds_exdf,
        forwardings.dfds_rfex,
        forwardings.dswb_exdf,
        forwardings.dswb_rfex);
   
}

void buildInstruction(){
    //R-Type Instructions---------------------------------------
    if (!strcmp(curr->opcode, "000000")){
        if (strcmp(curr->iText, "BREAK") && strcmp(curr->iText, "NOP")){
            if (!strcmp(curr->iText, "SUB" )||
                !strcmp(curr->iText, "SUBU")||
                !strcmp(curr->iText, "ADD" )||
                !strcmp(curr->iText, "ADDU")||
                !strcmp(curr->iText, "AND" )||
                !strcmp(curr->iText, "OR"  )||
                !strcmp(curr->iText, "XOR" )||
                !strcmp(curr->iText, "NOR" )||
                !strcmp(curr->iText, "SLT" )){
                sprintf(curr->assemblyString, "%s\tR%d, R%d, R%d", curr->iText, curr->ird, curr->irs, curr->irt);
            }
            else if (!strcmp(curr->iText, "SLL")){
                sprintf(curr->assemblyString, "%s\tR%d, R%d, R%d", curr->iText, curr->ird, curr->irt, curr->isa);
            }
            else if (!strcmp(curr->iText, "JR")){
                sprintf(curr->assemblyString, "%s\t#%d", curr->iText, curr->irs);
            }
        }
        else {
            sprintf(curr->assemblyString, "%s", curr->iText);
        }
    }

    //Jump
    else if (!strcmp(curr->iText, "J")){
        sprintf(curr->assemblyString, "%s\t#%d", curr->iText, curr->iTarget*4);
    }
    //Other
    else if (strcmp(curr->opcode, "000011") &&
         strcmp(curr->opcode, "010000") &&
         strcmp(curr->opcode, "010001") &&
         strcmp(curr->opcode, "010010") &&
         strcmp(curr->opcode, "010011")){
            if (!strcmp(curr->iText, "ADDI")  ||
                !strcmp(curr->iText, "ADDIU") ||
                !strcmp(curr->iText, "SLTI")){
                sprintf(curr->assemblyString, "%s\tR%d, R%d,#%d", curr->iText, curr->irt, curr->irs, curr->sImm);
            }
            if (!strcmp(curr->iText, "SW") ||
                !strcmp(curr->iText, "LW")){
                sprintf(curr->assemblyString, "%s\tR%d, %d(R%d)",curr->iText, curr->irt, curr->sImm, curr->irs);
            }
            if (!strcmp(curr->iText, "BNE") ||
                !strcmp(curr->iText, "BEQ")){
                sprintf(curr->assemblyString, "%s\tR%d, R%d,#%d", curr->iText, curr->irs, curr->irt, curr->sImm*4);
            }
            if (!strcmp(curr->iText, "BGEZ") ||
                !strcmp(curr->iText, "BGTZ") ||
                !strcmp(curr->iText, "BLEZ") ||
                !strcmp(curr->iText, "BLTZ")){
                sprintf(curr->assemblyString, "%s\tR%d,#%d", curr->iText, curr->irs, curr->sImm*4);
            }
    }
}

void storeInstruction(){

    int i, j, temp = 0;

    //Fill curr->binString
    strcpy(curr->binString, line);

    //Fill curr->branchPrediction
    curr->branchPrediction = false;

    //Remove any '\r'
    for (i=32; i>=0; i--){
        if(curr->binString[i]=='\r') {
            curr->binString[i] = '\0';
        }
    }
   
    //Fill curr->binStringX
    strcpy(curr->binStringX, curr->binString);
    //Inserts ' ' for proper formatting in disassembler
    for (i = 37; i >  6; i--) curr->binStringX[i] = curr->binStringX[i-1]; curr->binStringX[ 6] = ' ';
    for (i = 37; i > 12; i--) curr->binStringX[i] = curr->binStringX[i-1]; curr->binStringX[12] = ' ';
    for (i = 37; i > 18; i--) curr->binStringX[i] = curr->binStringX[i-1]; curr->binStringX[18] = ' ';
    for (i = 37; i > 24; i--) curr->binStringX[i] = curr->binStringX[i-1]; curr->binStringX[24] = ' ';
    for (i = 37; i > 30; i--) curr->binStringX[i] = curr->binStringX[i-1]; curr->binStringX[30] = ' ';


    //Fill curr->hexString
    getHexString();

    //Fill curr->opcode
    for (i = 0; i < 6; i++)
        curr->opcode[i] = line[i];

//R-Type Instructions
    if (!strcmp(curr->opcode, "000000")){
        //Set type 0
        curr->type = 0;
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 21; i++)
            curr->rd[i-16] = line[i];
        for (i = 21; i < 26; i++)
            curr->sa[i-21] = line[i];
        for (i = 26; i < 32; i++)
            curr->function[i-26] = line[i];

        if      (!strcmp(curr->function, "100000"))
            strcpy(curr->iText, "ADD");
        else if (!strcmp(curr->function, "100010"))
            strcpy(curr->iText, "SUB");
        else if (!strcmp(curr->function, "100011"))
            strcpy(curr->iText, "SUBU");
        else if (!strcmp(curr->function, "100001"))
            strcpy(curr->iText, "ADDU");
        else if (!strcmp(curr->function, "000010"))
            strcpy(curr->iText, "SRL");
        else if (!strcmp(curr->function, "000011"))
            strcpy(curr->iText, "SRA");
        else if (!strcmp(curr->function, "001000"))
            strcpy(curr->iText, "JR");
        else if (!strcmp(curr->function, "100100"))
            strcpy(curr->iText, "AND");
        else if (!strcmp(curr->function, "100101"))
            strcpy(curr->iText, "OR");
        else if (!strcmp(curr->function, "100110"))
            strcpy(curr->iText, "XOR");
        else if (!strcmp(curr->function, "100111"))
            strcpy(curr->iText, "NOR");
        else if (!strcmp(curr->function, "101010"))
            strcpy(curr->iText, "SLT");
        else if (!strcmp(curr->function, "001101"))
            strcpy(curr->iText, "BREAK");
        else if (!strcmp(curr->function, "000000")){
                if (!strcmp(curr->rs, "00000")){
                    strcpy(curr->iText, "SLL");
                    curr->isa = bin2Dec(curr->sa, 5);
                }
                if (!strcmp(curr->rd, "00000") &&
                    !strcmp(curr->rt, "00000") &&
                    !strcmp(curr->sa, "00000")){
                    strcpy(curr->iText, "NOP");
                    curr->NOP = true;
                }
            }

        curr->irs = bin2Dec(curr->rs, 5);
        curr->irt = bin2Dec(curr->rt, 5);
        curr->ird = bin2Dec(curr->rd, 5);
    }

//J-Type Instructions
    else if (!strcmp(curr->opcode, "000010")){
        curr->type = 1;
        for (i = 6; i < 32; i++)
            curr->target[i-6] = line[i];
        strcpy(curr->iText,"J");
        curr->iTarget = bin2Dec(curr->target, 26);
    }


//I-Type Instructions
    //ADDI
    else {
        curr->type = 2;
   
    if (!strcmp(curr->opcode, "001000")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"ADDI");
    }
    //ADDIU
    if (!strcmp(curr->opcode, "001001")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"ADDIU");
    }
    //SW
    if (!strcmp(curr->opcode, "101011")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"SW");
    }
    //LW
    if (!strcmp(curr->opcode, "100011")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"LW");
    }
    //BEQ
    if (!strcmp(curr->opcode, "000100")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"BEQ");
    }
    //BNE
    if (!strcmp(curr->opcode, "000101")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"BNE");
    }
    //BGEZ & BLTZ
    if (!strcmp(curr->opcode, "000001")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        if (!strcmp(curr->rt, "00000"))
            strcpy(curr->iText,"BLTZ");        
        else if (!strcmp(curr->iText,"00001"))
            strcpy(curr->iText,"BGEZ");
    }
    //BGTZ
    if (!strcmp(curr->opcode, "000111")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"BGTZ");
    }
    //BLEZ
    if (!strcmp(curr->opcode, "000110")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"BLEZ");
    }
    //SLTI
    if (!strcmp(curr->opcode, "001010")){
        for (i = 6; i < 11; i++)
            curr->rs[i-6] = line[i];
        for (i = 11; i < 16; i++)
            curr->rt[i-11] = line[i];
        for (i = 16; i < 32; i++)
            curr->immediate[i-16] = line[i];
        strcpy(curr->iText,"SLTI");
    }

    curr->irs  = bin2Dec(curr->rs, 5);
    curr->irt  = bin2Dec(curr->rt, 5);
    curr->sImm = bin2Dec(curr->immediate, 16);
   
    }

}

int bin2Dec(char name[], int length){
    int i = 0;
    int j = 0;
    int temp = 0;
    for (i = length; i > 0; i--){
        if (name[i-1] == '1')
            temp += (i==1?-1:1) *pow(2,j);
        j++;
    }
    return temp;
}

void getHexString(){
    int i, j, tHex;
    char tempFour[4];
    for (i = 0;i < 8; i++){
        tHex = 0;
        strncpy(tempFour, curr->binString+(i*4), 4);
        for (j = 0; j < 4; j++)
            tHex += (tempFour[j] - '0') << (3 - j);

        sprintf(curr->hexString + i, "%X", tHex);
    }
    for (i = 11; i > 2; i--)
        curr->hexString[i] = curr->hexString[i-1];
    curr->hexString[2] = ' ';
    for (i = 11; i > 5; i--)
        curr->hexString[i] = curr->hexString[i-1];
    curr->hexString[5] = ' ';
    for (i = 11; i > 8; i--)
        curr->hexString[i] = curr->hexString[i-1];
    curr->hexString[8] = ' ';
   
} 
