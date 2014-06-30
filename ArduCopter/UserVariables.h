/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

// user defined variables

// example variables used in Wii camera testing - replace with your own
// variables
#ifdef USERHOOK_VARIABLES

int strobe_cnt;
int startup_cnt;

unsigned int O2;
unsigned int CO2;

union b16 {
    int N;
    unsigned char B[2];
};

union b16 CO;
union b16 NO;
union b16 NO2;
union b16 SO2;
union b16 H2S;    
union b16 COC;
union b16 NOC;
union b16 NO2C;
union b16 SO2C;
union b16 H2SC;
    

#endif  // USERHOOK_VARIABLES


