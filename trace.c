#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "trace.h"

#define DUMP_FILE "/dev/shm/cstraceitm.bin"

#define WRITE_STM_AXI_PORT        // Comment this to trace hardware events only

#define FIFO_THRESHOLD     0x800  // Stop acquisition when FIFO reaches this threshold
#define TRACEID            0x20   // STM TRACEID (ATID on ATB bus) 

#define TIMESTAMP_EN       1      // Enable timestamp generation
#define SYNC_INTERVAL      100    // SYNC packet is emitted every 8*SYNC_INTERVAL bytes
#define CS_TIMESTAMP       1      // Coresight TS generation. Set this to 1 if using timestamps or they will stuck to 0
#define EN_FORMATTER       1      // This adds TRACEID field to ATB stream



void writeReg(uint32_t address, uint32_t value)
{
  *((volatile uint32_t*)address) = value;
}

void writeReg64(uint32_t address, uint64_t value)
{
  *((volatile uint32_t*)address) = value;
}

uint32_t readReg(uint32_t address)
{
  return *((volatile uint32_t*)address);
}


/* Dumps to file data read from ETB */
int dumpToFile(int fd, uint32_t data32)
{
  int ndata = 0;
  uint8_t data8;

  data8 = data32 & 0xff;
  ndata += write(fd, &data8, 1);
  data8 = (data32>>8) & 0xff;
  ndata += write(fd, &data8, 1);
  data8 = (data32>>16) & 0xff;
  ndata += write(fd, &data8, 1);
  data8 = (data32>>24) & 0xff;
  ndata += write(fd, &data8, 1);

  if(ndata != 4){
    fprintf(stderr, "Error writing data to stdout: wrote %i bytes instead of 4\n", ndata);
    exit(-1);
  }

  return 0;
}


/** Configures DBGMCU, STM, Funnel and ETF
 * Writes to first stimulus port until the FIFO reaches a given threshold
 * Dumps to standard output and to file ETF contents so they can be analyzed by OpenCSD tool
 */
int testSTM(void)
{
  int i;
  uint32_t data32;

  remove(DUMP_FILE);

  int fd = open(DUMP_FILE, O_RDWR | O_CREAT);
  if(fd < 0){
    fprintf(stderr, "Error opnening file\n");
    exit(fd);
  } 
  
  /**** Configure DBGMCU ****/
  /* Reads zero on silicon: possibly need to enable proper RISAF. Reads correct value on STICE4 */
  printf("DBGMCU_IDCODE %08x\n", readReg(DBGMCU_BASE));
  printf("DBGMCU_SR     %08x\n", readReg(DBGMCU_BASE + 0x0fc));

  writeReg(DBGMCU_BASE + 0x4, (1<<20) | (1<<21)); //DBGMCU_CR - Enable TRACECLKEN

  /**** Configure CoreSight Timestamp generator ****/
  writeReg(TSGEN_BASE + 0x20, 0x1000); //TSG_CNTFID0
  writeReg(TSGEN_BASE + 0x0, CS_TIMESTAMP); //TSG_CNTCR
 

  /**** Configure System Trace Macrocell ****/
  writeReg(STM_BASE + 0xfb0, 0xC5ACCE55); //STM_LAR
  
  /* Make sure it's stopped */
  writeReg(STM_BASE + 0xe80, 0);

  printf("STM_HEFEAT1R %08x\n", readReg(STM_BASE + 0xDF8)); //reads ok

  
  /* Enable first stimulus port */
  writeReg(STM_BASE + 0xE00, 0x1); //STM_SPER
  writeReg(STM_BASE + 0xE60, (1<<20) | 0x3); //STM_SPSCR
  writeReg(STM_BASE + 0xE64, (0x80<<16) | 0x1); //STM_SPMSCR

  /* Enable hardware port associated to UART4_irq (ttySTM0) and UART3_irq (ttySTM1) signals */
  writeReg(STM_BASE + 0xD00, (1<<20) | (1<<7)); //STM_SPTER
 
  /* Set HEBS and EVENT to monitor console's ttySTM0 UART4 interrupt (HEBS=1, EXTMUX=2). See table on spec */
  writeReg(STM_BASE + 0xD60, 1); //STM_HEBS
  writeReg(STM_BASE + 0xD68, 2); //STM_HEEXTMUXR
  
  /* Enable hardware events */
  writeReg(STM_BASE + 0xD64, 1 | (1<<7));

  
  writeReg(STM_BASE + 0xE70, (1<<3) | (11<<4)); //STM_SPTRIGCSR

  writeReg(STM_BASE + 0xE94, 1); //STM_AUXCR
  
  /* Set sync packets interval to 8*SYNC_INTERVAL bytes */
  writeReg(STM_BASE + 0xe90, (SYNC_INTERVAL << 3)); //STM_SYNCR
 
  /* Enable STM-500, set TRACEID and enable timestamp emission (TSEN) */
  writeReg(STM_BASE + 0xe80, 5 | (TRACEID << 16) | (TIMESTAMP_EN << 1));          //STM_TCSR  

  /* Print out configuration. Needed for decoder */
  printf("STM_TCSR: %08x\n", readReg(STM_BASE + 0xe80));

  /**** Configure CoreSight Trace Funnel ****/
  writeReg(CSTF_BASE + 0xfb0, 0xC5ACCE55); //CSTF_LAR

  /* Funnel STM only */
  writeReg(CSTF_BASE + 0x0, 0x301); //CSTF_CTRL

  printf("CSTF_CTRL  %08x\n", readReg(CSTF_BASE + 0x0));


  /**** Configure ETF ****/
  writeReg(ETF_BASE + 0xfb0, 0xC5ACCE55);

  /* Make sure it's stopped */
  writeReg(ETF_BASE + 0x20, 0); //ETF_CTL

  //writeReg(ETF_BASE + 0x28, 2);  //ETF_MODE in Hardware FIFO: data is passed over to ATB master port
  writeReg(ETF_BASE + 0x28, 0);  //ETF_MODE in circular buffer mode (no data is passed to ATB master port, but can be read via registers by processor)
  //writeReg(ETF_BASE + 0x28, 1); //ETF_MODE in software fifo mode

  /* Enable formatter. This adds TRACEID field to ATB stream (first stream byte >> 1) */
  writeReg(ETF_BASE + 0x304, EN_FORMATTER); //ETF_FFCR
 
  /* Enable ETF */
  writeReg(ETF_BASE + 0x20, readReg(ETF_BASE + 0x20) | 1); //ETF_CTL
  
  /**** Send data over STM or just wait for hardware triggers to come from ttySTM0 ****/
  i = 0;
  unsigned int stimulus_port = 0x0;
  uint32_t vals[16];
  while(1){
    

#ifdef WRITE_STM_AXI_PORT
    /* Write to STM and check when FIFO gets filled */
    writeReg(STM_CHANNELS  + stimulus_port, 0xabcd0000 + i);
#endif

    usleep(5000);

    /* Print some statistics */
    vals[0] = readReg(ETF_BASE + 0x0c); //ETF_STS
    vals[1] = readReg(ETF_BASE + 0x2c); //ETF_LBUFLVL
    vals[2] = readReg(ETF_BASE + 0x30); //ETF_CBUFLVL
    
    //printf("ETF_STS     %08x\n", vals[0]);    
    //printf("ETF_LBUFLVL %08x\n", vals[1]);
    //printf("ETF_CBUFLVL %08x\n", vals[2]);

    
    /* Break if running for too long or if ETF has some data in it */
    if((vals[2] >= FIFO_THRESHOLD))
      break;
   
    i++;
  }

  printf("Stopped after %i iterations\n", i);

  printf("ETF FIFO level %i\n", readReg(ETF_BASE + 0x30));

  unsigned int fifolevel = readReg(ETF_BASE + 0x30); 
 
  /* Stop ETF and read its contents */
  writeReg(ETF_BASE + 0x20, 0); //ETF_CTL

  printf("ETF FIFO level %i\n", readReg(ETF_BASE + 0x30));
 
  usleep(5000);

  for(i=0; i<fifolevel; i++){
    /* Read FIFO */
    data32 = readReg(ETF_BASE + 0x10); //ETF_RRD
    printf("%08x\n", data32);

    /* Write to file */
    dumpToFile(fd, data32);
  }
 
  close(fd);
  chmod(DUMP_FILE, 0777);

  /* Stop STM */
  writeReg(STM_BASE + 0xe80, 0);  

  return 0;
}



int main(void)
{
  int memfd;
  void* memarea;

  if ((memfd = open("/dev/mem", O_RDWR)) < 0) {
    fprintf(stderr, "Failed to open /dev/mem : %s\n", strerror(-memfd));
    return memfd;
  }

  /* Map CoreSight components */
  memarea = mmap((void*)0x50000000, 0x100000, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, memfd, 0x50000000);
  printf("memarea %p\n", memarea);
  if(memarea == (void*)-1){
    fprintf(stderr, "Failed to mmap requested region: %s\n", strerror(errno));
    return -errno;
  }

  printf("TPIU reg test %08x\n", readReg(0x500a0df8));

  /* Map STM AXI stumulus port space */
  memarea = mmap((void*)STM_CHANNELS, 0x100000, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, memfd, 0x90000000);
  printf("memarea %p\n", memarea);
  if(memarea == (void*)-1){
    fprintf(stderr, "Failed to mmap requested region: %s\n", strerror(errno));
    return -errno;
  }

  testSTM();
}
