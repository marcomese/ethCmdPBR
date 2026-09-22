#include <stdarg.h>
#include "commands.h"
#include "gps.h"

#define COUNT(ARRAY) (sizeof(ARRAY) / sizeof(*ARRAY))

/* status_register, fixed block (bit 13..0), from command_decoder.vhd */
#define RUN_POS         0U   /* running (run active: run cmd + all selected zynq busy low) */
#define RUNCTRLBUSY_POS 1U   /* runCtrlBusy    */
#define PLAXISBUSY_POS  2U   /* plToAxiSBusy: adapter busy OR downstream FIFO not ready */
#define FIFOFULL_POS    3U   /* fifoFull       */
#define BUSYCMD_POS     4U   /* cmd_busy       */
#define PPSTRG_POS      5U   /* pps_trg        */
#define GPSAUTO_POS     6U   /* pps_auto       */
#define FSMSTATE_POS    7U   /* fsmState (4 bit) -> bit 10..7 */
#define TIMEOUT_POS     11U  /* run start timed out waiting for the zynq busy lines */
#define GTUSEL_POS      12U  /* internal GTU selected   */
#define CLK40SEL_POS    13U  /* internal clk40M selected */

/* parametric block, stacked from bit 14 */
#define PARAM_BASE      14U
#define XGAMMA_POS      (PARAM_BASE)                  /* ZYNQ_NUM   bit */
#define TIMEOUTFLAG_POS (XGAMMA_POS      + ZYNQ_NUM)  /* ZYNQ_NUM   bit */
#define EXTTRG_POS      (TIMEOUTFLAG_POS + ZYNQ_NUM)  /* EXTTRG_NUM bit */
#define PPSEN_POS       (EXTTRG_POS      + EXTTRG_NUM)/* PPS_NUM    bit */
#define PPSPRES_POS     (PPSEN_POS       + PPS_NUM)   /* PPS_NUM    bit */
#define ZQEN_POS        (PPSPRES_POS     + PPS_NUM)   /* ZYNQ_NUM   bit */
#define ZQBUSY_POS      (ZQEN_POS        + ZYNQ_NUM)  /* ZYNQ_NUM   bit */
#define STATUS_USED     (ZQBUSY_POS      + ZYNQ_NUM)

#define STATUS_LEN     64U

_Static_assert(STATUS_USED <= STATUS_LEN, "status_register fields exceed STATUS_LEN");

#define RUN_CTRL_POS  FSMSTATE_POS
#define RUN_CTRL_MASK (0x0FU << RUN_CTRL_POS)

/* channel codes, NUM(0..6) in command_decoder.vhd */
static const uint8_t NUM[ZYNQ_NUM] = {0x0F, 0x33, 0x55, 0x66, 0x99, 0xAA, 0xCC};

_Static_assert(ZYNQ_NUM <= 7, "NUM[] holds at most 7 channel codes");

const char *errStr = "Error invalid command.\n";

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
    "BUSYZQMNG",
    "",
    "",
    "ERR",
};

static void appendBit(char* dst, const char* label, uint64_t reg, uint8_t pos){
    char tempStr[STATUS_ID_MAX_LEN] = "";

    snprintf(tempStr, STATUS_ID_MAX_LEN, "%.*s=%d ", STATUS_ID_STR_MAXLEN, label,
             (int)((reg >> pos) & 1U));

    strncat(dst, tempStr, STATUS_ID_MAX_LEN);
}

static void appendField(char* dst, const char* label, uint64_t reg, uint8_t base, uint8_t n){
    char tempStr[STATUS_ID_MAX_LEN] = "";

    for(uint8_t k = 0; k < n; k++){
        snprintf(tempStr, STATUS_ID_MAX_LEN, "%.*s%u=%d ", STATUS_ID_STR_MAXLEN, label, k,
                 (int)((reg >> (base + k)) & 1U));

        strncat(dst, tempStr, STATUS_ID_MAX_LEN);
    }
}

/* xGammaChannel is one hot: "OFF", "CH<nn>", or the raw field if more than one bit is set */
static void appendXGamma(char* dst, uint64_t reg){
    char tempStr[STATUS_ID_MAX_LEN] = "";
    unsigned int field = (unsigned int)((reg >> XGAMMA_POS) & ((1U << ZYNQ_NUM) - 1U));

    if(field == 0)
        snprintf(tempStr, STATUS_ID_MAX_LEN, "XGAMMA=OFF ");
    else if((field & (field - 1U)) == 0)
        snprintf(tempStr, STATUS_ID_MAX_LEN, "XGAMMA=CH%02d ", __builtin_ctz(field));
    else
        snprintf(tempStr, STATUS_ID_MAX_LEN, "XGAMMA=0x%02X ", field);

    strncat(dst, tempStr, STATUS_ID_MAX_LEN);
}

/* MASTERSLAVE register: "MSTR" / "SLV " written by command_decoder.vhd */
static const char* modeStr(uint32_t masterSlave){
    if(masterSlave == MASTERSLAVE_MSTR)
        return "MASTER";
    if(masterSlave == MASTERSLAVE_SLV)
        return "SLAVE";
    return "UNKNOWN";
}

/* PERIODS register -> gtu period and self trigger period in ns (0 = off) */
static void decodePeriods(uint32_t periods, unsigned long long* gtuNs, unsigned long long* selfNs){
    unsigned int gtuCyc = (periods >> PRD_GTU_POS) & PRD_GTU_MASK;
    unsigned int scale  = (periods >> PRD_SELF_SCALE_POS) & PRD_SELF_SCALE_MASK;
    unsigned int count  = (periods >> PRD_SELF_COUNT_POS) & PRD_SELF_COUNT_MASK;
    unsigned long long unit = PL_CLK_NS;

    for(unsigned int k = 0; k < scale; k++)
        unit *= 10;

    *gtuNs  = (unsigned long long)gtuCyc * PL_CLK_NS;
    *selfNs = (unsigned long long)count * unit;
}

static void decodeStatusReg(uint64_t statusReg, uint32_t masterSlave, uint32_t periods, char* statusStr){
    uint8_t runCtrlState = 0;
    unsigned long long gtuNs = 0, selfNs = 0;
    char resStr[TCP_SND_BUF] = "";
    char tempStr[STATUS_ID_MAX_LEN] = "";

    runCtrlState = (uint8_t)((statusReg & RUN_CTRL_MASK) >> RUN_CTRL_POS);
    decodePeriods(periods, &gtuNs, &selfNs);

    appendBit(resStr, "RUN",        statusReg, RUN_POS);
    appendBit(resStr, "BUSY",       statusReg, RUNCTRLBUSY_POS);
    appendBit(resStr, "PLAXISBUSY", statusReg, PLAXISBUSY_POS);
    appendBit(resStr, "FIFOFULL",   statusReg, FIFOFULL_POS);
    appendBit(resStr, "BUSYCMD",    statusReg, BUSYCMD_POS);
    appendBit(resStr, "PPSTRGON",   statusReg, PPSTRG_POS);
    appendBit(resStr, "GPSAUTO",    statusReg, GPSAUTO_POS);
    appendBit(resStr, "TIMEOUT",    statusReg, TIMEOUT_POS);
    appendBit(resStr, "GTUINT",     statusReg, GTUSEL_POS);
    appendBit(resStr, "CLK40INT",   statusReg, CLK40SEL_POS);

    appendXGamma(resStr, statusReg);
    appendField(resStr, "TOUTFLG", statusReg, TIMEOUTFLAG_POS, ZYNQ_NUM);
    appendField(resStr, "EXTTRG",  statusReg, EXTTRG_POS,      EXTTRG_NUM);
    appendField(resStr, "PPSEN",   statusReg, PPSEN_POS,       PPS_NUM);
    appendField(resStr, "PPSPRES", statusReg, PPSPRES_POS,     PPS_NUM);
    appendField(resStr, "ZQ",      statusReg, ZQEN_POS,        ZYNQ_NUM);
    appendField(resStr, "ZQBUSY",  statusReg, ZQBUSY_POS,      ZYNQ_NUM);

    snprintf(tempStr, STATUS_ID_MAX_LEN, "MODE=%s GTUPERIOD=%lluns ", modeStr(masterSlave), gtuNs);
    strncat(resStr, tempStr, STATUS_ID_MAX_LEN);

    if(selfNs == 0)
        snprintf(tempStr, STATUS_ID_MAX_LEN, "SELFTRG=OFF ");
    else
        snprintf(tempStr, STATUS_ID_MAX_LEN, "SELFTRG=%lluns ", selfNs);
    strncat(resStr, tempStr, STATUS_ID_MAX_LEN);

    snprintf(tempStr, STATUS_ID_MAX_LEN, "RUNCTRL=%s\n", runCtrlDecode[runCtrlState]);

    strncat(resStr, tempStr, STATUS_ID_MAX_LEN);

    strncpy(statusStr, resStr, TCP_SND_BUF);
}

/* unsigned long is 32 bit on the Zynq: 64 bit keeps periods up to 8 s */
static int parseUInt(const char* s, unsigned long long* val){
    char* end = NULL;

    if(s == NULL || *s == '\0')
        return -1;

    errno = 0;
    *val = strtoull(s, &end, 0);

    return (errno == 0 && *end == '\0') ? 0 : -1;
}

/* "<n>" -> NUM[n], "all" -> ARG_ALL (idx = -1); returns -1 on bad input */
static int parseChannel(const char* s, uint8_t* code, int* idx){
    unsigned long long n = 0;

    if(strcmp(s, "all") == 0){
        *code = ARG_ALL;
        *idx  = -1;
        return 0;
    }

    if(parseUInt(s, &n) != 0 || n >= ZYNQ_NUM)
        return -1;

    *code = NUM[n];
    *idx  = (int)n;
    return 0;
}

/* enable/on -> ARG_ON, disable/off -> ARG_OFF */
static int parseOnOff(const char* s, uint8_t* code){
    if(strcmp(s, "enable") == 0 || strcmp(s, "on") == 0){
        *code = ARG_ON;
        return 0;
    }
    if(strcmp(s, "disable") == 0 || strcmp(s, "off") == 0){
        *code = ARG_OFF;
        return 0;
    }
    return -1;
}

/* gtu internal <ns> -> period in clk cycles */
static int encodeGtuPeriod(unsigned long long ns, uint16_t* cycles, char* reply){
    if(ns % PL_CLK_NS != 0){
        snprintf(reply, TCP_SND_BUF, "Error: GTU period must be a multiple of %lu ns\n", PL_CLK_NS);
        return -1;
    }

    unsigned long long cyc = ns / PL_CLK_NS;

    if(cyc < GTU_PERIOD_MIN_CYC || cyc > GTU_PERIOD_MAX_CYC){
        snprintf(reply, TCP_SND_BUF, "Error: GTU period out of range (%lu..%lu ns)\n",
                 GTU_PERIOD_MIN_CYC * PL_CLK_NS, GTU_PERIOD_MAX_CYC * PL_CLK_NS);
        return -1;
    }

    *cycles = (uint16_t)cyc;
    return 0;
}

/* trg self <ns> -> finest scale on which the period is an exact count of units:
 * scale k = 10^k * PL_CLK_NS, count 1..SELF_COUNT_MAX */
static int encodeSelfPeriod(unsigned long long ns, uint16_t* field, char* reply){
    unsigned long long unit = PL_CLK_NS;

    for(unsigned int scale = 0; scale < SELF_SCALE_NUM; scale++, unit *= 10){
        if(ns % unit != 0)
            continue;

        unsigned long long count = ns / unit;

        if(count >= 1 && count <= SELF_COUNT_MAX){
            *field = (uint16_t)((scale << SELF_SCALE_POS) | count);
            return 0;
        }
    }

    snprintf(reply, TCP_SND_BUF,
             "Error: self trigger period not representable (%lu ns .. %llu ns, "
             "multiple of the scale step)\n",
             PL_CLK_NS, (unsigned long long)SELF_COUNT_MAX * PL_CLK_NS * 100000ULL);
    return -1;
}

static uint32_t sendPl(axiRegisters_t* regDev, uint32_t word, const char* echo, char* reply){
    writeReg(regDev->ctrlReg, CTRL_REG_ADDR, CMD_RECV_ADDR, word);
    snprintf(reply, TCP_SND_BUF, "%s\n", echo);
    return word;
}

static uint32_t readPl(axiRegisters_t* regDev, uint32_t baseAddr, uint32_t regAddr){
    return readReg(regionOf(regDev, baseAddr), baseAddr, regAddr);
}

static uint32_t readL1(axiRegisters_t* regDev, int ch){
    if(ch < 4)
        return readPl(regDev, L1CNT_03_REG_ADDR, L1_0_COUNTER_ADDR + 4U * (uint32_t)ch);

    return readPl(regDev, L1CNT_46_REG_ADDR, L1_4_COUNTER_ADDR + 4U * (uint32_t)(ch - 4));
}

static void appendLine(char* reply, const char* fmt, ...){
    char line[STATUS_ID_MAX_LEN] = "";
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    strncat(reply, line, TCP_SND_BUF - strlen(reply) - 1);
}

typedef uint32_t (*cmdFunc_t)(axiRegisters_t* regDev, int argc, char** argv, char* reply);

/* a command that answers with raw bytes instead of text sets this to the reply length */
static size_t binReplyLen = 0;

static uint32_t usage(const char* text, char* reply){
    snprintf(reply, TCP_SND_BUF, "Error: usage: %s\n", text);
    return CMD_ERROR;
}

static uint32_t cmdRun(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    if(argc == 2 && strcmp(argv[1], "start") == 0)
        return sendPl(regDev, PL_CMD(CMD_RUN, ARG_ON, 0, 0), "RUN START", reply);
    if(argc == 2 && strcmp(argv[1], "stop") == 0)
        return sendPl(regDev, PL_CMD(CMD_RUN, ARG_OFF, 0, 0), "RUN STOP", reply);

    return usage("run start|stop", reply);
}

static uint32_t cmdBusy(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    if(argc == 2 && strcmp(argv[1], "set") == 0)
        return sendPl(regDev, PL_CMD(CMD_BSY, ARG_ON, 0, 0), "BUSY SET", reply);
    if(argc == 2 && strcmp(argv[1], "release") == 0)
        return sendPl(regDev, PL_CMD(CMD_BSY, ARG_OFF, 0, 0), "BUSY RELEASE", reply);

    return usage("busy set|release", reply);
}

/* PERIODS register read, ns */
static void readPeriods(axiRegisters_t* regDev, unsigned long long* gtuNs, unsigned long long* selfNs){
    decodePeriods(readPl(regDev, L1CNT_46_REG_ADDR, PERIODS_ADDR), gtuNs, selfNs);
}

static uint32_t cmdTrg(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    const char* use = "trg soft | trg external|pps|clkb enable|disable | trg normal | trg self <ns>|0|period";
    uint8_t onOff = 0;
    unsigned long long ns = 0, gtuNs = 0, selfNs = 0;
    uint16_t field = 0;
    char echo[STATUS_ID_MAX_LEN] = "";

    if(argc == 2 && strcmp(argv[1], "soft") == 0)
        return sendPl(regDev, PL_CMD(CMD_TRG, ARG_ON, 0, 0), "TRG SOFT", reply);

    if(argc == 3 && strcmp(argv[1], "self") == 0 && strcmp(argv[2], "period") == 0){
        readPeriods(regDev, &gtuNs, &selfNs);

        if(selfNs == 0)
            snprintf(reply, TCP_SND_BUF, "TRG SELF PERIOD=OFF\n");
        else
            snprintf(reply, TCP_SND_BUF, "TRG SELF PERIOD=%lluns\n", selfNs);

        return CMD_LOCAL;
    }

    if(argc == 2 && strcmp(argv[1], "normal") == 0)
        return sendPl(regDev, PL_CMD(CMD_TRG, ARG_NORMAL, 0, 0), "TRG NORMAL", reply);

    if(argc == 3 && parseOnOff(argv[2], &onOff) == 0){
        uint8_t src = 0;

        if(strcmp(argv[1], "external") == 0)
            src = ARG_OFF;
        else if(strcmp(argv[1], "pps") == 0)
            src = ARG_PPS;
        else if(strcmp(argv[1], "clkb") == 0)
            src = ARG_CLKB;
        else
            return usage(use, reply);

        snprintf(echo, sizeof(echo), "TRG %s %s", argv[1], onOff == ARG_ON ? "ENABLE" : "DISABLE");
        return sendPl(regDev, PL_CMD(CMD_TRG, src, onOff, 0), echo, reply);
    }

    if(argc == 3 && strcmp(argv[1], "self") == 0){
        if(parseUInt(argv[2], &ns) != 0)
            return usage(use, reply);

        if(ns == 0)
            return sendPl(regDev, PL_CMD(CMD_TRG, ARG_SELF, 0, 0), "TRG SELF OFF", reply);

        if(encodeSelfPeriod(ns, &field, reply) != 0)
            return CMD_ERROR;

        snprintf(echo, sizeof(echo), "TRG SELF %llu ns", ns);
        return sendPl(regDev, PL_CMD(CMD_TRG, ARG_SELF, field >> 8, field & 0xFF), echo, reply);
    }

    return usage(use, reply);
}

static uint32_t cmdGps(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    unsigned long long hi = 0, lo = 0;
    uint8_t conf[GPS_CONF_LEN];

    if(argc != 4 || strcmp(argv[1], "configure") != 0 ||
       parseUInt(argv[2], &hi) != 0 || parseUInt(argv[3], &lo) != 0 ||
       hi > 0xFFFFFFFFUL || lo > 0xFFFFFFFFUL)
        return usage("gps configure <word hi> <word lo> (32 bit each, sent big endian)", reply);

    /* the 8 bytes go to the UART as they are, most significant byte first */
    for(int i = 0; i < 4; i++){
        conf[i]     = (uint8_t)(hi >> (24 - 8 * i));
        conf[4 + i] = (uint8_t)(lo >> (24 - 8 * i));
    }
    gpsSetConf(conf);

    return sendPl(regDev, PL_CMD(CMD_GPS, ARG_ON, 0, 0), "GPS CONFIGURE", reply);
}

static uint32_t cmdPps(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    unsigned long long n = 0;

    if(argc == 3 && strcmp(argv[1], "gps") == 0 && parseUInt(argv[2], &n) == 0 && (n == 1 || n == 2)){
        char echo[STATUS_ID_MAX_LEN] = "";

        snprintf(echo, sizeof(echo), "PPS GPS %llu", n);
        return sendPl(regDev, PL_CMD(CMD_PPS, ARG_ON, n == 1 ? ARG_ON : ARG_OFF, 0), echo, reply);
    }
    if(argc == 2 && strcmp(argv[1], "clkb") == 0)
        return sendPl(regDev, PL_CMD(CMD_PPS, ARG_OFF, 0, 0), "PPS CLKB", reply);
    if(argc == 2 && strcmp(argv[1], "auto") == 0)
        return sendPl(regDev, PL_CMD(CMD_PPS, ARG_PPS, 0, 0), "PPS AUTO", reply);

    return usage("pps gps 1|2 | pps clkb | pps auto", reply);
}

static uint32_t cmdGtu(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    unsigned long long ns = 0;
    uint16_t cycles = 0;

    if(argc == 3 && strcmp(argv[1], "internal") == 0 && parseUInt(argv[2], &ns) == 0){
        char echo[STATUS_ID_MAX_LEN] = "";

        if(encodeGtuPeriod(ns, &cycles, reply) != 0)
            return CMD_ERROR;

        snprintf(echo, sizeof(echo), "GTU INTERNAL %llu ns", ns);
        return sendPl(regDev, PL_CMD(CMD_GTU, ARG_ON, cycles >> 8, cycles & 0xFF), echo, reply);
    }
    if(argc == 2 && strcmp(argv[1], "external") == 0)
        return sendPl(regDev, PL_CMD(CMD_GTU, ARG_OFF, 0, 0), "GTU EXTERNAL", reply);

    if(argc == 2 && strcmp(argv[1], "period") == 0){
        unsigned long long gtuNs = 0, selfNs = 0;

        readPeriods(regDev, &gtuNs, &selfNs);
        snprintf(reply, TCP_SND_BUF, "GTU PERIOD=%lluns\n", gtuNs);
        return CMD_LOCAL;
    }

    return usage("gtu internal <ns> | gtu external | gtu period", reply);
}

static uint32_t readRateL1(axiRegisters_t* regDev, int ch){
    if(ch < 4)
        return readPl(regDev, RATE_L103_REG_ADDR, RATE_L1_0_ADDR + 4U * (uint32_t)ch);

    return readPl(regDev, RATE_L146_REG_ADDR, RATE_L1_4_ADDR + 4U * (uint32_t)(ch - 4));
}

/* rate [all] | rate l1 <n>|all | rate trg ext|clkb|out | rate gtu | rate clk40m
 * every value is the number of edges seen in the last 1 s gate (Hz) */
static uint32_t cmdRate(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    const char* use = "rate [all] | rate l1 <n>|all | rate trg ext|clkb|out | rate gtu | rate clk40m";
    int all = (argc == 1 || (argc == 2 && strcmp(argv[1], "all") == 0));

    reply[0] = '\0';

    if(all || (argc == 3 && strcmp(argv[1], "l1") == 0)){
        int idx = -1;
        uint8_t code = 0;

        if(!all && parseChannel(argv[2], &code, &idx) != 0)
            return usage(use, reply);

        if(idx >= 0){
            appendLine(reply, "L1_%d RATE=%" PRIu32 "\n", idx, readRateL1(regDev, idx));
            return CMD_LOCAL;
        }
        for(int ch = 0; ch < ZYNQ_NUM; ch++)
            appendLine(reply, "L1_%d RATE=%" PRIu32 "\n", ch, readRateL1(regDev, ch));
        if(!all)
            return CMD_LOCAL;
    }

    if(all || (argc == 3 && strcmp(argv[1], "trg") == 0)){
        int ext  = all || strcmp(argv[2], "ext")  == 0;
        int clkb = all || strcmp(argv[2], "clkb") == 0;
        int out  = all || strcmp(argv[2], "out")  == 0;

        if(!ext && !clkb && !out)
            return usage(use, reply);

        if(ext)
            appendLine(reply, "TRG EXT RATE=%" PRIu32 "\n",  readPl(regDev, RATE_L146_REG_ADDR,   RATE_EXTJTRG_ADDR));
        if(clkb)
            appendLine(reply, "TRG CLKB RATE=%" PRIu32 "\n", readPl(regDev, RATE_CLKTRG_REG_ADDR, RATE_EXTCLKB_ADDR));
        if(out)
            appendLine(reply, "TRG OUT RATE=%" PRIu32 "\n",  readPl(regDev, RATE_CLKTRG_REG_ADDR, RATE_TRGOUT_ADDR));
        if(!all)
            return CMD_LOCAL;
    }

    if(all || (argc == 2 && strcmp(argv[1], "gtu") == 0)){
        appendLine(reply, "GTU RATE=%" PRIu32 "\n", readPl(regDev, RATE_CLKTRG_REG_ADDR, RATE_GTU_ADDR));
        if(!all)
            return CMD_LOCAL;
    }

    if(all || (argc == 2 && strcmp(argv[1], "clk40m") == 0)){
        appendLine(reply, "CLK40M RATE=%" PRIu32 "\n", readPl(regDev, RATE_CLKTRG_REG_ADDR, RATE_CLK40_ADDR));
        return CMD_LOCAL;
    }

    return usage(use, reply);
}

static uint32_t cmdMode(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    (void)argv;

    if(argc != 1)
        return usage("mode", reply);

    snprintf(reply, TCP_SND_BUF, "MODE=%s\n", modeStr(readPl(regDev, ALIVEDEAD_REG_ADDR, MASTERSLAVE_ADDR)));
    return CMD_LOCAL;
}

static uint32_t cmdClk40m(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    if(argc == 2 && strcmp(argv[1], "internal") == 0)
        return sendPl(regDev, PL_CMD(CMD_40M, ARG_ON, 0, 0), "CLK40M INTERNAL", reply);
    if(argc == 2 && strcmp(argv[1], "external") == 0)
        return sendPl(regDev, PL_CMD(CMD_40M, ARG_OFF, 0, 0), "CLK40M EXTERNAL", reply);

    return usage("clk40m internal|external", reply);
}

/* counter l1 <n>|all [reset] | counter evt|gtu|all [reset]
 * without "reset" the counters are read here, with it the PL clears them */
static uint32_t cmdCounter(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    const char* use = "counter l1 <n>|all [reset] | counter evt|gtu|all [reset]";
    int reset = (argc >= 3 && strcmp(argv[argc - 1], "reset") == 0);
    int nArgs = argc - reset;   /* tokens before the optional "reset" */

    if(nArgs == 3 && strcmp(argv[1], "l1") == 0){
        uint8_t code = 0;
        int idx = -1;

        if(parseChannel(argv[2], &code, &idx) != 0)
            return usage(use, reply);

        if(reset){
            char echo[STATUS_ID_MAX_LEN] = "";

            snprintf(echo, sizeof(echo), "COUNTER L1 %s RESET", argv[2]);
            return sendPl(regDev, PL_CMD(CMD_CNT, ARG_ON, code, ARG_ON), echo, reply);
        }

        reply[0] = '\0';
        if(idx < 0){
            for(int ch = 0; ch < ZYNQ_NUM; ch++)
                appendLine(reply, "L1_%d COUNTER=%" PRIu32 "\n", ch, readL1(regDev, ch));
        }else{
            appendLine(reply, "L1_%d COUNTER=%" PRIu32 "\n", idx, readL1(regDev, idx));
        }
        return CMD_LOCAL;
    }

    if(nArgs == 2){
        uint8_t act = 0;

        if(strcmp(argv[1], "evt") == 0)
            act = ARG_OFF;
        else if(strcmp(argv[1], "gtu") == 0)
            act = ARG_PPS;
        else if(strcmp(argv[1], "all") == 0)
            act = ARG_NORMAL;
        else
            return usage(use, reply);

        if(reset){
            char echo[STATUS_ID_MAX_LEN] = "";

            snprintf(echo, sizeof(echo), "COUNTER %s RESET", argv[1]);
            return sendPl(regDev, PL_CMD(CMD_CNT, act, ARG_ON, 0), echo, reply);
        }

        reply[0] = '\0';
        if(act == ARG_OFF || act == ARG_NORMAL)
            appendLine(reply, "EVT COUNTER=%" PRIu32 "\n", readPl(regDev, CNT_REG_ADDR, EVT_COUNTER_ADDR));
        if(act == ARG_PPS || act == ARG_NORMAL)
            appendLine(reply, "GTU COUNTER=%" PRIu32 "\n", readPl(regDev, CNT_REG_ADDR, GTU_COUNTER_ADDR));
        if(act == ARG_NORMAL){
            appendLine(reply, "PPS COUNTER=%" PRIu32 "\n", readPl(regDev, CNT_REG_ADDR, PPS_COUNTER_ADDR));
            appendLine(reply, "CLK40M COUNTER=%" PRIu32 "\n", readPl(regDev, CNT_REG_ADDR, CLK40_COUNTER_ADDR));
            for(int ch = 0; ch < ZYNQ_NUM; ch++)
                appendLine(reply, "L1_%d COUNTER=%" PRIu32 "\n", ch, readL1(regDev, ch));
        }
        return CMD_LOCAL;
    }

    return usage(use, reply);
}

static uint32_t cmdCh(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    const char* use = "ch enable|disable <n>|all | ch xgamma on|off <n>";
    uint8_t onOff = 0, code = 0;
    int idx = -1;
    char echo[STATUS_ID_MAX_LEN] = "";

    if(argc == 3 && parseOnOff(argv[1], &onOff) == 0 && parseChannel(argv[2], &code, &idx) == 0){
        snprintf(echo, sizeof(echo), "CH %s %s", onOff == ARG_ON ? "ENABLE" : "DISABLE", argv[2]);
        return sendPl(regDev, PL_CMD(CMD_CHN, onOff, code, 0), echo, reply);
    }

    if(argc >= 2 && strcmp(argv[1], "xgamma") == 0){
        if(argc == 4 && strcmp(argv[2], "on") == 0 && parseChannel(argv[3], &code, &idx) == 0 && idx >= 0){
            snprintf(echo, sizeof(echo), "CH XGAMMA ON %d", idx);
            return sendPl(regDev, PL_CMD(CMD_CHN, ARG_PPS, ARG_ON, code), echo, reply);
        }
        if(argc == 3 && strcmp(argv[2], "off") == 0)
            return sendPl(regDev, PL_CMD(CMD_CHN, ARG_PPS, ARG_OFF, 0), "CH XGAMMA OFF", reply);
    }

    return usage(use, reply);
}

static uint32_t cmdStatus(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    /* the two halves are read back to back, not atomically: fine for monitoring */
    uint64_t status = readPl(regDev, STATUS_REG_ADDR, STATUS_LO_ADDR);
    status |= (uint64_t)readPl(regDev, STATUS_REG_ADDR, STATUS_HI_ADDR) << 32;
    uint32_t masterSlave = readPl(regDev, ALIVEDEAD_REG_ADDR, MASTERSLAVE_ADDR);
    uint32_t periods     = readPl(regDev, L1CNT_46_REG_ADDR, PERIODS_ADDR);

    if(argc == 1){
        decodeStatusReg(status, masterSlave, periods, reply);
        return CMD_LOCAL;
    }
    if(argc == 2 && strcmp(argv[1], "raw") == 0){
        /* binary reply: the 64 bit status register as it is in memory (little endian,
         * same byte order as statusLo/statusHi in the data record), no text */
        memcpy(reply, &status, sizeof(status));
        binReplyLen = sizeof(status);
        return CMD_LOCAL;
    }

    return usage("status [raw]", reply);
}

static uint32_t cmdFw(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    if(argc != 2 || strcmp(argv[1], "sha") != 0)
        return usage("fw sha", reply);

    uint32_t usr = readPl(regDev, STATUS_REG_ADDR, FW_SHA_ADDR);

    if(usr == 0)
        snprintf(reply, TCP_SND_BUF, "FW SHA=0000000 (non-reproducible build)\n");
    else
        snprintf(reply, TCP_SND_BUF, "FW SHA=%07" PRIx32 "\n", usr & FW_SHA_MASK);

    return CMD_LOCAL;
}

static uint32_t cmdExit(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    (void)regDev; (void)argc; (void)argv;

    snprintf(reply, TCP_SND_BUF, "EXIT\n");
    return EXIT;
}

static uint32_t cmdHelp(axiRegisters_t* regDev, int argc, char** argv, char* reply);

typedef struct{
    const char* name;
    cmdFunc_t   func;
    const char* usage;
} family_t;

static const family_t families[] = {
    {"run",     cmdRun,     "run start|stop"},
    {"busy",    cmdBusy,    "busy set|release"},
    {"trg",     cmdTrg,     "trg soft | trg external|pps|clkb enable|disable | trg normal | trg self <ns>|0|period"},
    {"gps",     cmdGps,     "gps configure <word hi> <word lo>"},
    {"pps",     cmdPps,     "pps gps 1|2 | pps clkb | pps auto"},
    {"gtu",     cmdGtu,     "gtu internal <ns> | gtu external | gtu period"},
    {"clk40m",  cmdClk40m,  "clk40m internal|external"},
    {"counter", cmdCounter, "counter l1 <n>|all [reset] | counter evt|gtu|all [reset]"},
    {"ch",      cmdCh,      "ch enable|disable <n>|all | ch xgamma on|off <n>"},
    {"rate",    cmdRate,    "rate [all] | rate l1 <n>|all | rate trg ext|clkb|out | rate gtu | rate clk40m"},
    {"mode",    cmdMode,    "mode"},
    {"status",  cmdStatus,  "status [raw]"},
    {"fw",      cmdFw,      "fw sha"},
    {"help",    cmdHelp,    "help"},
    {"exit",    cmdExit,    "exit"},
};

static uint32_t cmdHelp(axiRegisters_t* regDev, int argc, char** argv, char* reply){
    (void)regDev; (void)argc; (void)argv;

    reply[0] = '\0';
    for(unsigned int i = 0; i < COUNT(families); i++)
        appendLine(reply, "%s\n", families[i].usage);

    return CMD_LOCAL;
}

uint32_t decodeCmdStr(axiRegisters_t* regDev, int connfd, char* cmdStr, int len){
    char* argv[CMD_MAX_ARGS + 1] = {0};
    int argc = 0;
    char* save = NULL;
    char reply[TCP_SND_BUF] = "";
    uint32_t ret = CMD_ERROR;

    if(len == 0)
        return CMD_ERROR;

    binReplyLen = 0;

    for(char* tok = strtok_r(cmdStr, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)){
        if(argc > CMD_MAX_ARGS){   /* too many tokens: fall through to the error */
            argc = 0;
            break;
        }
        argv[argc++] = tok;
    }

    if(argc > 0){
        for(unsigned int i = 0; i < COUNT(families); i++){
            if(strcmp(argv[0], families[i].name) == 0){
                ret = families[i].func(regDev, argc, argv, reply);
                break;
            }
        }
    }

    if(ret == CMD_ERROR && reply[0] == '\0')
        snprintf(reply, TCP_SND_BUF, "%s", errStr);

    if(binReplyLen > 0){
        printf("BIN REPLY:");
        for(size_t i = 0; i < binReplyLen; i++)
            printf(" %02X", (unsigned char)reply[i]);
        printf("\n");
        write(connfd, reply, binReplyLen);
    }else{
        printf("%s", reply);
        write(connfd, reply, strlen(reply));
    }

    return ret;
}
