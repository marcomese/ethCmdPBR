#include "commands.h"

#define COUNT(ARRAY) (sizeof(ARRAY) / sizeof(*ARRAY))

#define RUN_CTRL_POS 16U
#define RUN_CTRL_MASK (0x0FU << RUN_CTRL_POS)

static uint8_t sorted = 0;

const char *errStr = "Error invalid command.\n";
const char *invalidAddr = "Error: invalid register address.\n";

const char statusIDStr[32][STATUS_ID_MAX_LEN] = {
    "RUN=",
    "BUSY=",
    "FIFOREADY=",
    "FIFOFULL=",
    "ZQ0=",
    "ZQ1=",
    "ZQ2=",
    "ZQ3=",
    "ZQBUSY0=",
    "ZQBUSY1=",
    "ZQBUSY2=",
    "ZQBUSY3=",
    "BUSYCMD=",
    "SELFTRGON=",
    "PPSTRGON=",
    "EXTTRGON=",
    "",
    "",
    "",
    "",
    "GPS1EN=",
    "GPS2EN=",
    "GPS1PRES=",
    "GPS2PRES=",
    "GPSAUTO=",
    "TRGPDM0=",
    "TRGPDM1=",
    "TRGPDM2=",
    "TRGPDM3="
};

const char runCtrlDecode[16][STATUS_ID_MAX_LEN] = {
    "IDLE",
    "STARTRUN",
    "WAITTRG",
    "TRGOR",
    "TRGCPU",
    "TRGEXT",
    "TRGPPS",
    "BUSYCPU",
    "BUSYZYNQ",
    "BUSY",
    "",
    "",
    "",
    "ERR",
};

static void decodeStatusReg(uint32_t statusReg, char* statusStr){
    uint8_t statusBit = 0;
    uint8_t runCtrlState = 0;
    char resStr[TCP_SND_BUF] = "";
    char tempStr[STATUS_ID_MAX_LEN] = "";

    runCtrlState = (statusReg & RUN_CTRL_MASK) >> RUN_CTRL_POS;

    for(int i = 0; i < 32; i++){
        if(strncmp(statusIDStr[i],"",STATUS_ID_MAX_LEN) != 0){
            memset(tempStr, '\0', STATUS_ID_MAX_LEN);
            
            statusBit = ((statusReg & (1 << i)) >> i) & 1;

            snprintf(tempStr, STATUS_ID_MAX_LEN, "%.*s%d ", STATUS_ID_STR_MAXLEN, statusIDStr[i], statusBit & 1);

            strncat(resStr, tempStr, STATUS_ID_MAX_LEN);
        }
    }

    snprintf(tempStr, STATUS_ID_MAX_LEN, "RUNCTRL=%s\n", runCtrlDecode[runCtrlState]);

    strncat(resStr, tempStr, STATUS_ID_MAX_LEN);

    strncpy(statusStr,resStr,TCP_SND_BUF);
}

static void writeCmd(axiRegisters_t *regDev, int connfd, cmd_t *c){
    writeReg(regDev->ctrlReg, c->baseAddr, c->regAddr, c->cmdVal);
    printf("%s", c->feedbackStr);
    write(connfd, c->feedbackStr, strlen(c->feedbackStr));
}

static void readCmd(axiRegisters_t *regDev, int connfd, cmd_t *c){
    uint32_t regVal = 0;
    char resStr[TCP_SND_BUF] = "";
    uint32_t* reg;
    
    switch(c->cmdVal){
        case READ_STATUS:
            reg = regDev->statusReg;
            regVal = readReg(reg, c->baseAddr, c->regAddr);
            decodeStatusReg(regVal,resStr);
            break;
        case READ_L10COUNTER:
        case READ_L11COUNTER:
        case READ_L12COUNTER:
        case READ_L13COUNTER:
            reg = regDev->l1CntReg;
            regVal = readReg(reg, c->baseAddr, c->regAddr);
            snprintf(resStr, TCP_SND_BUF, "%s%u\n", c->feedbackStr, (unsigned int)regVal);
            break;
        case READ_EVTCOUNTER:
        case READ_GTUCOUNTER:
            reg = regDev->statusReg;
            regVal = readReg(reg, c->baseAddr, c->regAddr);
            snprintf(resStr, TCP_SND_BUF, "%s%u\n", c->feedbackStr, (unsigned int)regVal);
            break;
        default:
            snprintf(resStr, TCP_SND_BUF, "%s", invalidAddr);
            break;
    }

    printf("%s", resStr);
    write(connfd, resStr, strlen(resStr));
}

static void echo(axiRegisters_t *regDev, int connfd, cmd_t *c){
    (void)c;
    (void)regDev;

    printf("%s", c->feedbackStr);
    write(connfd, c->feedbackStr, strlen(c->feedbackStr));
}

static void help(axiRegisters_t *regDev, int connfd, cmd_t *c);

static cmd_t commands[] = {
    {"start run",     START_RUN,       "START RUN\n",       writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Start the acquisition run"},
    {"stop run",      STOP_RUN,        "STOP RUN\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Stop the acquisition run"},
    {"rel busy",      RELEASE_BUSY,    "RELEASE BUSY\n",    writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Release the clkBoard busy signal"},
    {"set busy",      SET_BUSY,        "SET BUSY\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Set the clkBoard busy signal"},
    {"trg",           TRIGGER,         "TRIGGER\n",         writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Send a software trigger signal"},
    {"gps reset",     RESET_GPS,       "RESET GPS\n",       writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Reset the GPSs (all of them)"},
    {"gps configure", CONFIGURE_GPS,   "CONFIGURE GPS\n",   writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Configure the GPSs (all of them)"},
    {"gps1 on",       GPS1_ON,         "GPS1 ON\n",         writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable the GPS1"},
    {"gps2 on",       GPS2_ON,         "GPS2 ON\n",         writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable the GPS2"},
    {"gps1 no",       GPS1_NO,         "NO GPS1\n",         writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable the GPS1"},
    {"gps2 no",       GPS2_NO,         "NO GPS2\n",         writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable the GPS2"},
    {"gpsauto on",    GPS_AUTO_ON,     "GPS AUTO ON\n",     writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable the auto selection of PPS (from GPS)"},
    {"gpsauto no",    GPS_AUTO_NO,     "GPS AUTO NO\n",     writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable the auto selection of PPS (from GPS)"},
    {"gtu reset",     RESET_GTU_COUNT, "RESET GTU COUNT\n", writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Reset the GTU counter"},
    {"evt reset",     RESET_EVT_COUNT, "RESET EVT COUNT\n", writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Reset the event counter"},
    {"l1 reset",      RESET_L1_COUNT,  "RESET L1 COUNT\n",  writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Reset the L1 counters"},
    {"all reset",     RESET_ALL_COUNT, "RESET ALL COUNT\n", writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Reset all the counters"},
    {"ppstrg on",     PPS_TRG_ON,      "PPS TRG ON\n",      writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable triggering on PPS"},
    {"ppstrg off",    PPS_TRG_OFF,     "PPS TRG OFF\n",     writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable triggering on PPS"},
    {"msk exttrg",    MASK_EXT_TRG,    "MASK EXT TRG\n",    writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Mask the external trigger"},
    {"usk exttrg",    UNMASK_EXT_TRG,  "UNMASK EXT TRG\n",  writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Unmask the external trigger"},
    {"trg self on",   SELF_TRG_ON,     "SELF TRG ON\n",     writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable the self triggering mode"},
    {"trg self off",  SELF_TRG_OFF,    "SELF TRG OFF\n",    writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disabel the self triggering mode"},
    {"zq0 no",        NO_ZYNQ0,        "NO ZYNQ0\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable Zynq 0"},
    {"zq1 no",        NO_ZYNQ1,        "NO ZYNQ1\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable Zynq 1"},
    {"zq2 no",        NO_ZYNQ2,        "NO ZYNQ2\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable Zynq 2"},
    {"zq3 no",        NO_ZYNQ3,        "NO ZYNQ3\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Disable Zynq 3"},
    {"zq0 on",        ZYNQ0_ON,        "ZYNQ0 ON\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable Zynq 0"},
    {"zq1 on",        ZYNQ1_ON,        "ZYNQ1 ON\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable Zynq 1"},
    {"zq2 on",        ZYNQ2_ON,        "ZYNQ2 ON\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable Zynq 2"},
    {"zq3 on",        ZYNQ3_ON,        "ZYNQ3 ON\n",        writeCmd, CTRL_REG_ADDR,   CMD_RECV_ADDR,     "Enable Zynq 3"},
    {"status",        READ_STATUS,     NULL,                readCmd,  STATUS_REG_ADDR, STATUS_REG_ADDR,   "Show the decoded status register"},
    {"gtu counter",   READ_GTUCOUNTER, "GTU COUNTER=",      readCmd,  STATUS_REG_ADDR, GTU_COUNTER_ADDR,  "Show the GTU counter"},
    {"evt counter",   READ_EVTCOUNTER, "EVT COUNTER=",      readCmd,  STATUS_REG_ADDR, EVT_COUNTER_ADDR,  "Show the event counter"},
    {"l10 counter",   READ_L10COUNTER, "L1_0 COUNTER=",     readCmd,  L1CNT_REG_ADDR,  L1_0_COUNTER_ADDR, "Show the L1_0 counter"},
    {"l11 counter",   READ_L11COUNTER, "L1_1 COUNTER=",     readCmd,  L1CNT_REG_ADDR,  L1_1_COUNTER_ADDR, "Show the L1_1 counter"},
    {"l12 counter",   READ_L12COUNTER, "L1_2 COUNTER=",     readCmd,  L1CNT_REG_ADDR,  L1_2_COUNTER_ADDR, "Show the L1_2 counter"},
    {"l13 counter",   READ_L13COUNTER, "L1_3 COUNTER=",     readCmd,  L1CNT_REG_ADDR,  L1_3_COUNTER_ADDR, "Show the L1_3 counter"},
    {"exit",          EXIT,            "EXIT\n",            echo,     NONE,            NONE,              "Exit and close the connection"},
    {"help",          HELP,            NULL,                help,     NONE,            NONE,              "Print this help message"}
};

static void help(axiRegisters_t *regDev, int connfd, cmd_t *c){
    (void)c;
    (void)regDev;

    for(unsigned int i = 0; i < COUNT(commands); i++){
        char cStr[CMD_MAX_LEN+DESC_MAX_LEN] = "";

        snprintf(cStr, CMD_MAX_LEN+DESC_MAX_LEN, "%s:\n\t%s\n", commands[i].cmdStr, commands[i].cmdDesc);

        printf(cStr);

        write(connfd, cStr, strlen(cStr));
    }
}

static int compare(const void *p1, const void *p2){
    return strcmp(*((const char **)p1), *((const char **)p2));
}

static cmd_t *getCmd(const char *name){
    if (!sorted){
        qsort(commands, COUNT(commands), sizeof(*commands), compare);
        sorted = 1;
    }

    cmd_t *item = (cmd_t *)bsearch(&name, commands, COUNT(commands), sizeof(*commands), compare);

    return item;
}

uint32_t decodeCmdStr(axiRegisters_t* regDev, int connfd, char *cmdStr, int len){
    if(len == 0)
        return NONE;

    cmd_t *cmd = getCmd(cmdStr);

    if (cmd != NULL){
        cmd->funcPtr(regDev, connfd, cmd);
        return cmd->cmdVal;
    }else{
        printf("%s", errStr);
        write(connfd, errStr, strlen(errStr));
    }

    return 0;
}