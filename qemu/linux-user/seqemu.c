#include <unistd.h>	// pread,fstat
#include <elf.h>	// ELF structure
#include <string.h>	// memcpy
#include <stdint.h>
#include <sys/types.h>	// fstat,open
#include <sys/stat.h>	// fstat,open
#include <fcntl.h>	// open
#include "seqemu.h"
//#include "qemu.h"	// image_info
//#include "include/exec/user/abitypes.h" // abi_ulong
//#include <stdio.h>	// fprintf

unsigned long seqemu_guest_base;
struct image_info seqemu_image_info;


void seqemu_print_debug(void){
    fprintf(stderr,"SEQEMU DEBUGGING!\n");
}

void seqemu_save_guest_base(unsigned long base){
    seqemu_guest_base = base;
}

void seqemu_print_guest_base(void){
    fprintf(stderr,"[DEBUG] GUEST BASE = %lx\n",seqemu_guest_base);
}


void seqemu_save_image_info(struct image_info *info){

	seqemu_image_info.start_brk = info->start_brk;
	seqemu_image_info.end_code = info->end_code;
	seqemu_image_info.start_code = info->start_code;
	seqemu_image_info.start_data = info->start_data;
	seqemu_image_info.end_data = info->end_data;
	seqemu_image_info.start_stack = info->start_stack;
	seqemu_image_info.brk = info->brk;
	seqemu_image_info.entry = info->entry;
	seqemu_image_info.arg_start = info->arg_start;
	seqemu_image_info.arg_end = info->arg_end;
	seqemu_image_info.saved_auxv = info->saved_auxv;
}

void seqemu_print_image_info(void){
	unsigned long b = seqemu_guest_base;
	fprintf(stderr,"start_brk   =0x%lx [0x%x]\n",seqemu_image_info.start_brk + b, seqemu_image_info.start_brk);
	fprintf(stderr,"end_code    =0x%lx [0x%x]\n",seqemu_image_info.end_code + b, seqemu_image_info.end_code);
	fprintf(stderr,"start_code  =0x%lx [0x%x]\n",seqemu_image_info.start_code + b, seqemu_image_info.start_code);
	fprintf(stderr,"start_data  =0x%lx [0x%x]\n",seqemu_image_info.start_data + b, seqemu_image_info.start_data);
	fprintf(stderr,"end_data    =0x%lx [0x%x]\n",seqemu_image_info.end_data + b, seqemu_image_info.end_data);
	fprintf(stderr,"start_stack =0x%lx [0x%x]\n",seqemu_image_info.start_stack + b, seqemu_image_info.start_stack);
	fprintf(stderr,"brk         =0x%lx [0x%x]\n",seqemu_image_info.brk + b, seqemu_image_info.brk);
	fprintf(stderr,"entry       =0x%lx [0x%x]\n",seqemu_image_info.entry + b, seqemu_image_info.entry);
	fprintf(stderr,"argv_start  =0x%lx [0x%x]\n",seqemu_image_info.arg_start + b, seqemu_image_info.arg_start);
	fprintf(stderr,"env_start   =0x%lx [0x%x]\n",seqemu_image_info.arg_end + (abi_ulong)sizeof(abi_ulong) + b, seqemu_image_info.arg_end + (abi_ulong)sizeof(abi_ulong));
	fprintf(stderr,"auxv_start  =0x%lx [0x%x]\n",seqemu_image_info.saved_auxv + b, seqemu_image_info.saved_auxv);
}

// feature-002 Filtering the Dangerous Functions


static const char *dangerous_func[SEQEMU_MAX_FUNCTION_NAME_LENGTH] = {
	"gets",
	""
};

static const char *format_func[SEQEMU_MAX_FUNCTION_NAME_LENGTH] = {
	"printf",
	"fprintf",
	""
};

static const char *malloc_func[SEQEMU_MAX_FUNCTION_NAME_LENGTH] = {
	"malloc",
	"free",
	""
};

static const char *buffer_func[SEQEMU_MAX_FUNCTION_NAME_LENGTH] = {
	"memcpy",
	""
};



void seqemu_bswap_2(void *p){
	uint8_t tmp;
	uint8_t *sp;

	sp = (uint8_t *)p;
	tmp = sp[0];
	sp[0] = sp[1];
	sp[1] = tmp;
}

void seqemu_bswap_4(void *p){
	uint8_t tmp;
	uint8_t *sp;

	sp = (uint8_t *)p;
	tmp = sp[0];
	sp[0] = sp[3];
	sp[3] = tmp;

	tmp = sp[1];
	sp[1] = sp[2];
	sp[2] = tmp;
}

void seqemu_read_elf(int fd){
	Elf32_Ehdr *elfh;
	Elf32_Shdr *sh,*sh_relplt,*sh_dynstr,*sh_shstrtab,*sh_dynsym;
	Elf32_Rel *rels;
	Elf32_Sym *syms;
	uint8_t buf[sizeof(Elf32_Ehdr)];
	uint8_t *shstrtab,*dynstr;
	int i,j,istyped;
	Seqemu_target_func *target_func;

	/////////////////////////////////////////////////
	// Step 1. Get ELF Header, and Checking the MAGIC
	/////////////////////////////////////////////////

	elfh = g_malloc(sizeof(Elf32_Ehdr));
	pread(fd,buf,sizeof(Elf32_Ehdr),0x0);
	if(!(buf[EI_MAG0] == ELFMAG0 &&
		buf[EI_MAG1] == ELFMAG1 &&
		buf[EI_MAG2] == ELFMAG2 &&
		buf[EI_MAG3] == ELFMAG3)){
		exit(-1);
	}

	memcpy(elfh,buf,sizeof(Elf32_Ehdr));

	/////////////////////////////
	// Step 2. Find the .shstrtab
	/////////////////////////////

	sh = g_malloc(sizeof(Elf32_Shdr));
	sh_shstrtab = g_malloc(sizeof(Elf32_Shdr));


	// Find the .shstrtab
	for(i = 0; i < elfh->e_shnum; i++){
		pread(fd,sh_shstrtab,sizeof(Elf32_Shdr),elfh->e_shoff + sizeof(Elf32_Shdr) * i);
		uint8_t bytes[2];
		pread(fd,bytes,sizeof(uint8_t) * 2,sh_shstrtab->sh_offset);
		if(bytes[0] == 0x0 && bytes[1] == 0x2e){
			break;
		}
	}

	// Read the Contents of .shstrtab
	shstrtab = g_malloc(sizeof(uint8_t) * sh_shstrtab->sh_size);
	pread(fd,shstrtab,sh_shstrtab->sh_size,sh_shstrtab->sh_offset);


	/////////////////////////////////////////////////////////
	// Step 3. Find the Sections (.rel.plt, .dynsym, .dynstr)
	/////////////////////////////////////////////////////////

	for(i = 0; i < elfh->e_shnum; i++){
		pread(fd,sh,sizeof(Elf32_Shdr),elfh->e_shoff + sizeof(Elf32_Shdr) * i);
		//fprintf(stderr,"[%02x] %x Addr:%x Off:%x Size:%x Es:%x Flg:%x Lk:%x Inf:%x Al:%x\n",i,sh->sh_type,sh->sh_addr,sh->sh_offset,sh->sh_size,sh->sh_entsize,sh->sh_flags,sh->sh_link,sh->sh_info,sh->sh_addralign);


		// Get Section of .rel.plt
		if(sh->sh_type == SHT_REL){
			if(strcmp((char *)&shstrtab[sh->sh_name],".rel.plt") == 0){
				fprintf(stderr,"[OK] .rel.plt is EXIST!\n");
				sh_relplt = g_malloc(sizeof(Elf32_Shdr));
				memcpy(sh_relplt,sh,sizeof(Elf32_Shdr));
			}
		}

		// Get Section of .dynsym
		if(sh->sh_type == SHT_DYNSYM){
			fprintf(stderr,"[OK] .dynsym is EXIST!\n");
			sh_dynsym = g_malloc(sizeof(Elf32_Shdr));
			memcpy(sh_dynsym,sh,sizeof(Elf32_Shdr));
		}

		// Get Section of .dynstr
		if(sh->sh_type == SHT_STRTAB && sh->sh_addr != 0x0){
			fprintf(stderr,"[OK] .dynstr is EXIST!\n");
			sh_dynstr = g_malloc(sizeof(Elf32_Shdr));
			memcpy(sh_dynstr,sh,sizeof(Elf32_Shdr));
		}

	}

	// Checking .rel.plt and .dynstr is exists
	if(sh_relplt == NULL || sh_dynsym == NULL || sh_dynstr == NULL){
		fprintf(stderr,"[Error] .rel.plt or .dynsym or .dynstr is not EXIST!\n");
	}

	///////////////////////////////////////
	// Step 4. Get Contents of Each Section
	///////////////////////////////////////

	// Get the dynstr
	dynstr = g_malloc(sizeof(uint8_t) * sh_dynstr->sh_size);
	pread(fd,dynstr,sizeof(uint8_t) * sh_dynstr->sh_size,sh_dynstr->sh_offset);

	// Get .rel.plt Contents
	rels = g_malloc(sh_relplt->sh_size);
	pread(fd,rels,sh_relplt->sh_size,sh_relplt->sh_offset);

	// Get .dynsym Contents
	syms = g_malloc(sh_dynsym->sh_size);
	pread(fd,syms,sh_dynsym->sh_size,sh_dynsym->sh_offset);


	/////////////////////////////////////////////////
	// Step 5. Construct The Dangerous Function Table
	/////////////////////////////////////////////////

	target_func = g_malloc(sizeof(Seqemu_target_func) * (sh_relplt->sh_size / sizeof(Elf32_Rel)));

	for(i = 0; i < sh_relplt->sh_size / sizeof(Elf32_Rel); i++){
		istyped = 0;
		target_func[i].addr = rels[i].r_offset;
		target_func[i].name = strdup((char *)&dynstr[syms[rels[i].r_info >> 8].st_name]);
		for(j = 0; dangerous_func[j] != NULL; j++){
			if(strcmp(dangerous_func[j],target_func[i].name) == 0){
				target_func[i].type = SEQEMU_FUNC_TYPE_DANGEROUS;
				istyped = 1;
			}
		}

		for(j = 0; format_func[j] != NULL; j++){
			if(strcmp(format_func[j],target_func[i].name) == 0){
				target_func[i].type = SEQEMU_FUNC_TYPE_FORMAT;
				istyped = 1;
			}
		}

		for(j = 0; malloc_func[j] != NULL; j++){
			if(strcmp(malloc_func[j],target_func[i].name) == 0){
				target_func[i].type = SEQEMU_FUNC_TYPE_MALLOC;
				istyped = 1;
			}
		}

		for(j = 0; buffer_func[j] != NULL; j++){
			if(strcmp(buffer_func[j],target_func[i].name) == 0){
				target_func[i].type = SEQEMU_FUNC_TYPE_BUFFER;
				istyped = 1;
			}
		}

		if(istyped == 0){
			target_func[i].type = SEQEMU_FUNC_TYPE_OTHER;
		}

	}

	for(i = 0; i < sh_relplt->sh_size / sizeof(Elf32_Rel); i++){
		fprintf(stderr,"[%d] name : %s, addr = 0x%x, type = %x\n",i,target_func[i].name,target_func[i].addr,target_func[i].type);
	}


	// If DANGEROUS FUNCTION is EXISTS, display Miho Kohinata
	

	for(i = 0; i < sh_relplt->sh_size / sizeof(Elf32_Rel); i++){
		if(target_func[i].type == SEQEMU_FUNC_TYPE_DANGEROUS){


			// Open dame.txt and get the file size

			struct stat stbuf;
			int kohinata_fd = open("../resources/dame.txt",O_RDONLY);
			long file_size;
			char *output_buffer;

			if(kohinata_fd == -1){
				fprintf(stderr,"[ERROR!] Can't open \"dame.txt\" !\n");
				exit(-1);
			}

			if(fstat(kohinata_fd,&stbuf) == -1){
				fprintf(stderr,"[ERROR!] Can't get the file size!\n");
				exit(-1);
			}

			file_size = stbuf.st_size;

			output_buffer = (char *)g_malloc(sizeof(char) * file_size);

			if(output_buffer == NULL){
				fprintf(stderr,"[ERROR!] Can't get the output_buffer memory!\n");
			}

			read(kohinata_fd,output_buffer,file_size);
			fprintf(stderr,"\n\n%s\n",output_buffer);
			fprintf(stderr,"\n[ABORT!] Dangerous function is EXIST!\n");
			exit(-1);
		}
	}


}
