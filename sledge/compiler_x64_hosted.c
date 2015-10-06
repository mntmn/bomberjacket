#if defined(__APPLE__) && defined(__MACH__)
#define MAP_ANONYMOUS MAP_ANON
#endif

#define DEBUG

#include <sys/mman.h> // mprotect
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

Cell* execute_jitted(void* binary) {
  return (Cell*)((funcptr)binary)(0);
}

int compile_for_platform(Cell* expr, Cell** res) {
  jit_out = fopen("/tmp/jit_out.s","w");
  
  jit_init();
  
  register void* sp asm ("sp");
  Frame empty_frame = {NULL, 0, 0, sp};
  int success = compile_expr(expr, &empty_frame, TAG_ANY);
  jit_ret();

  if (!success) {
    printf("<compile_expr failed: %d>\r\n",success);
  }

  if (success) {
    int codesz = 1024;
    fclose(jit_out);

    struct stat src_stat;
    stat("/tmp/jit_out.s", &src_stat);
    off_t generated_sz = src_stat.st_size;

    FILE* asm_f = fopen("/tmp/jit_out.s","r");
    uint32_t* jit_asm = malloc(generated_sz);
    memset(jit_asm,0,generated_sz);
    fread(jit_asm,1,generated_sz,asm_f);
    fclose(asm_f);
        
#ifdef DEBUG
    printf("\nJIT ---------------------\n%s-------------------------\n\n",jit_asm);
#endif
    free(jit_asm);
        
    // prefix with arm-none-eabi- on ARM  -mlittle-endian
    
    system("as -o /tmp/jit_out.o /tmp/jit_out.s -L");
#if defined(__APPLE__) && defined(__MACH__)
    system("gobjcopy /tmp/jit_out.o -O binary /tmp/jit_out.bin");
#else
    system("objcopy /tmp/jit_out.o -O binary /tmp/jit_out.bin");
#endif

    stat("/tmp/jit_out.bin", &src_stat);
    
    generated_sz = src_stat.st_size;
    while (generated_sz>codesz) {
      codesz*=2;
      printf ("<compiler: doubling code block size to %d>\r\n",codesz);
    }
    
    FILE* binary_f = fopen("/tmp/jit_out.bin","r");
    
    uint32_t* jit_binary = mmap(0, codesz, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    
    int bytes_read = fread(jit_binary,1,codesz,binary_f);
    fclose(binary_f);

#ifdef DEBUG
    printf("<assembled bytes: %d at: %p>\n",bytes_read,jit_binary);
    char cmd[256];
    sprintf(cmd,"cp /tmp/jit_out.o /tmp/jit_%p.o",jit_binary);
    system(cmd);
    sprintf(cmd,"cp /tmp/jit_out.s /tmp/jit_%p.s",jit_binary);
    system(cmd);
#endif

    if (bytes_read>codesz) {
      printf("<error: max assembly size of %d exhausted. aborting>\n",codesz);
      munmap(jit_binary,codesz);
      return 0;
    }

    // read symbols for linking lambdas
#if defined(__APPLE__) && defined(__MACH__)
    system("gnm /tmp/jit_out.o > /tmp/jit_out.syms 2> /dev/null");
#else
    system("nm /tmp/jit_out.o > /tmp/jit_out.syms");
#endif
    FILE* link_f = fopen("/tmp/jit_out.syms","r");
    if (link_f) {
      char* link_line=malloc(128);
      while(fgets(link_line, 128, link_f)) {

        if (strlen(link_line)>22) {
          char ida=link_line[19];
          char idb=link_line[20];
          char idc=link_line[21];
          printf("link_line: %s %c %c %c\n",link_line,ida,idb,idc);

          if (ida=='L' && idc=='_') {
            Cell* lambda = (Cell*)strtoul(&link_line[24], NULL, 16);
            if (idb=='0') {
              // function entrypoint
              // TODO: 64/32 bit
              unsigned long long offset = strtoul(link_line, NULL, 16);
              void* binary = ((uint8_t*)jit_binary) + offset;
              printf("function %p entrypoint: %p (+%ld)\n",lambda,binary,offset);

              if (lambda->tag == TAG_LAMBDA) {
                lambda->dr.next = binary;
              } else {
                printf("fatal error: no lambda found at %p!\n",lambda);
              }
            }
            else if (idb=='1') {
              // function exit
              unsigned long long offset = strtoul(link_line, NULL, 16);
              printf("function exit point: %p\n",offset);
            }
          }
        }
      }
      free(link_line);
    }

    int mp_res = mprotect(jit_binary, codesz, PROT_EXEC|PROT_READ);
    
    if (!mp_res) {

#ifdef DEBUG
    printf("checking the bug %p>\n",jit_binary);
#endif
      *res = execute_jitted(jit_binary);
#ifdef DEBUG
    printf("checking the bug %p>\n",res);
#endif
      success = 1;
    } else {
      printf("<mprotect result: %d\n>",mp_res);
      *res = NULL;
      success = 0;
    }
  }
  return success;
}
