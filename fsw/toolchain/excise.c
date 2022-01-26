#include <config.h>
#include <bfd.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BFD_TARGET "elf32-littlearm"

// #define EXCISE_DEBUG

static const char *excise_sections[] = {
    ".data",
    ".bss",
    "initpoints",
    "tasktable",
    "replicas",
    NULL,
};

static bool excise_section(asection *sec) {
    if (sec == bfd_com_section_ptr) {
        return true;
    }
    const char *name = bfd_section_name(sec);
    for (const char **section = excise_sections; *section != NULL; section++) {
        if (strcmp(name, *section) == 0) {
            return true;
        }
    }
    return false;
}

struct callback_context {
    bfd      *ob;
    asymbol **input_symbols;
    size_t    input_symbol_count;
    asymbol **output_symbols;
    size_t    output_symbol_count;
    bool      failed;
    bool      unsafe;
};

static void init_section(bfd *ib, asection *isec, void *opaque) {
    struct callback_context *ctx = (struct callback_context *) opaque;
    bfd *ob = ctx->ob;

    if (excise_section(isec)) {
        /* skip this section */
        return;
    }

    asection *osec = bfd_make_section_with_flags(ob, bfd_section_name(isec), bfd_section_flags(isec));
    if (osec == NULL) {
        bfd_perror("Section creation failed");
        ctx->failed = true;
        return;
    }
    if (!bfd_set_section_size(osec, bfd_convert_section_size(ib, isec, ob, bfd_section_size(isec)))) {
        bfd_perror("Section set size failed");
        ctx->failed = true;
        return;
    }
    if (!bfd_set_section_vma(osec, bfd_section_vma(isec))) {
        bfd_perror("Section set VMA failed");
        ctx->failed = true;
        return;
    }
    if (!bfd_set_section_lma(osec, bfd_section_lma(isec))) {
        bfd_perror("Section set LMA failed");
        ctx->failed = true;
        return;
    }
    if (!bfd_set_section_alignment(osec, bfd_section_alignment(isec))) {
        bfd_perror("Section set alignment failed");
        ctx->failed = true;
        return;
    }
    osec->entsize = isec->entsize;
    osec->compress_status = isec->compress_status;
    isec->output_section = osec;
    isec->output_offset = 0;
    if (!bfd_copy_private_section_data(ib, isec, ob, osec)) {
        bfd_perror("Section private data copy failed");
        ctx->failed = true;
        return;
    }
}

static bool fix_symbols(bfd *ob, asymbol **input_symbols, size_t input_symbol_count, asymbol **output_symbols, size_t *output_symbol_count) {
    size_t max_in = input_symbol_count;
    size_t out = 0;
    for (size_t i = 0; i < max_in; i++) {
        asymbol *sym = input_symbols[i];
        if (excise_section(sym->section)) {
#ifdef EXCISE_DEBUG
            printf("Undefining symbol: %s\n", sym->name);
#endif
            if (strcmp(sym->name, "$d") != 0 && !(sym->flags & BSF_SECTION_SYM)) {
                asymbol *nsym = bfd_make_empty_symbol(ob);
                nsym->name = sym->name;
                nsym->value = sym->value;
                nsym->flags = sym->flags & ~(BSF_LOCAL | BSF_GLOBAL | BSF_FUNCTION | BSF_OBJECT);
                nsym->section = bfd_und_section_ptr;
                output_symbols[out++] = nsym;
            }
        } else {
            output_symbols[out++] = sym;
        }
    }
    *output_symbol_count = out;
    return true;
}

static asymbol **lookup_symbol(struct callback_context *ctx, asymbol *symbol) {
    if (symbol->flags & BSF_LOCAL) {
        if (!ctx->unsafe) {
            fprintf(stderr, "WARNING: unlikely to be able to reference replacement symbol %s\n", symbol->name);
        }
        ctx->unsafe = true;
    }
    for (size_t i = 0; i < ctx->output_symbol_count; i++) {
        if (strcmp(ctx->output_symbols[i]->name, symbol->name) == 0) {
            return &ctx->output_symbols[i];
        }
    }
    return NULL;
}

static asymbol **replace_symbol(struct callback_context *ctx, asymbol *symbol) {
    /* section references will have been stripped, in which case we have to pivot to looking up what the  */
    /* first thing was in the input section, because that's probably what was meant.                      */
    if (symbol->flags & BSF_SECTION_SYM) {
        for (size_t i = 0; i < ctx->input_symbol_count; i++) {
            if (ctx->input_symbols[i]->section == symbol->section
                    && ctx->input_symbols[i]->value == 0
                    && (ctx->input_symbols[i]->flags & BSF_SECTION_SYM) == 0
                    && ctx->input_symbols[i]->name[0] != '$') {
#ifdef EXCISE_DEBUG
                printf("Replacement selected for %s: %s\n", symbol->name, ctx->input_symbols[i]->name);
#endif
                return lookup_symbol(ctx, ctx->input_symbols[i]);
            }
        }
    } else {
        return lookup_symbol(ctx, symbol);
    }
}

static void copy_section(bfd *ib, asection *isec, void *opaque) {
    struct callback_context *ctx = (struct callback_context *) opaque;
    bfd *ob = ctx->ob;
    asection *osec = isec->output_section;

    if (excise_section(isec)) {
        /* skip this section */
        return;
    }

    ssize_t relocation_bytes = bfd_get_reloc_upper_bound(ib, isec);
    if (relocation_bytes < 0) {
        bfd_perror("Get relocation table upper bound failed");
        ctx->failed = true;
        return;
    }
    arelent **relocs = bfd_alloc(ob, relocation_bytes);
    if (relocs == NULL) {
        bfd_perror("Allocation failed for relocations");
        ctx->failed = true;
        return;
    }
    ssize_t reloc_count = bfd_canonicalize_reloc(ib, isec, relocs, ctx->input_symbols);
    if (reloc_count < 0) {
        bfd_perror("Canonicalize relocations failed");
        ctx->failed = true;
        return;
    }
    for (size_t i = 0; i < reloc_count; i++) {
        arelent *rl = relocs[i];
        if (excise_section((*rl->sym_ptr_ptr)->section)) {
            const char *symname = (*rl->sym_ptr_ptr)->name;
#ifdef EXCISE_DEBUG
            printf("Disrupted relocation for: %s\n", symname);
#endif
            asymbol **newsym = replace_symbol(ctx, *rl->sym_ptr_ptr);
            if (newsym == NULL) {
#ifdef EXCISE_DEBUG
                fprintf(stderr, "Could not find symbol at all: %s\n", symname);
#endif
                ctx->failed = true;
                return;
            }
            rl->sym_ptr_ptr = newsym;
        }
    }
    bfd_set_reloc(ob, osec, relocs, reloc_count);

    if (bfd_section_flags(isec) & SEC_HAS_CONTENTS) {
        size_t size = bfd_section_size(isec);
        uint8_t *bytes = malloc(size);
        if (bytes == NULL) {
#ifdef EXCISE_DEBUG
            fprintf(stderr, "Could not allocate data bytes.\n");
#endif
            ctx->failed = true;
            return;
        }
        if (!bfd_get_section_contents(ib, isec, bytes, 0, size)) {
            bfd_perror("Could not retrieve section bytes");
            ctx->failed = true;
            free(bytes);
            return;
        }
        uint8_t *converted = bytes;
        size_t convert_size = size;
        if (!bfd_convert_section_contents(ib, isec, ob, &converted, &convert_size)) {
            bfd_perror("Could not convert section contents");
            ctx->failed = true;
            free(bytes);
            return;
        }
        if (!bfd_set_section_contents(ob, osec, converted, 0, convert_size)) {
            bfd_perror("Could not set section contents");
            ctx->failed = true;
            return;
        }
        free(bytes);
    }
}

enum filter_status {
    FILTER_OK,       /* success */
    FILTER_FAILED,   /* BFD or other error; not able to complete request */
    FILTER_REJECTED, /* this .o file cannot be safely excised */
};

static enum filter_status filter_elf(bfd *ib, bfd *ob) {
    if (!bfd_check_format(ib, bfd_object)) {
        bfd_perror("Format check failed");
        return FILTER_FAILED;
    }
    if (!bfd_set_format(ob, bfd_object)) {
        bfd_perror("Format set failed");
        return FILTER_FAILED;
    }
    if (!bfd_set_file_flags(ob, bfd_get_file_flags(ib))) {
        bfd_perror("Flag set failed");
        return FILTER_FAILED;
    }
    if (!bfd_set_arch_mach(ob, bfd_get_arch(ib), bfd_get_mach(ib))) {
        bfd_perror("Arch/Mach set failed");
        return FILTER_FAILED;
    }
    if (bfd_get_flavour(ib) != bfd_target_elf_flavour || bfd_get_flavour(ob) != bfd_target_elf_flavour) {
        bfd_perror("Flavour is not ELF");
        return FILTER_FAILED;
    }
    struct callback_context init_section_ctx = {
        .ob = ob,
        .failed = false,
    };
    bfd_map_over_sections(ib, init_section, &init_section_ctx);
    if (init_section_ctx.failed) {
        fprintf(stderr, "Could not create all sections.\n");
        return FILTER_FAILED;
    }
    if (!bfd_copy_private_header_data(ib, ob)) {
        bfd_perror("Private header data copy failed");
        return FILTER_FAILED;
    }
    ssize_t input_symbol_bytes = bfd_get_symtab_upper_bound(ib);
    if (input_symbol_bytes < 0) {
        bfd_perror("Get symbol table upper bound failed");
        return FILTER_FAILED;
    }
    /* must be allocated on 'ob' so that it sticks around until 'ob' is written out */
    asymbol **input_symbols = bfd_alloc(ob, input_symbol_bytes);
    asymbol **output_symbols = bfd_alloc(ob, input_symbol_bytes);
    if (input_symbols == NULL || output_symbols == NULL) {
        bfd_perror("Allocation failed for canonicalized symbol table");
        return FILTER_FAILED;
    }
    ssize_t input_symbol_count = bfd_canonicalize_symtab(ib, input_symbols);
    if (input_symbol_count < 0) {
        bfd_perror("Canonicalize symbol table failed");
        return FILTER_FAILED;
    }
    size_t output_symbol_count = 0;
    if (!fix_symbols(ob, input_symbols, input_symbol_count, output_symbols, &output_symbol_count)) {
        fprintf(stderr, "Could not fix symbols.\n");
        return FILTER_FAILED;
    }
    if (!bfd_set_symtab(ob, output_symbols, output_symbol_count)) {
        bfd_perror("Output symbol count failed");
        return FILTER_FAILED;
    }
    struct callback_context copy_section_ctx = {
        .ob = ob,
        .input_symbols = input_symbols,
        .input_symbol_count = input_symbol_count,
        .output_symbols = output_symbols,
        .output_symbol_count = output_symbol_count,
        .failed = false,
        .unsafe = false,
    };
    bfd_map_over_sections(ib, copy_section, &copy_section_ctx);
    if (copy_section_ctx.failed) {
        fprintf(stderr, "Could not copy all sections.\n");
        return FILTER_FAILED;
    }
    if (copy_section_ctx.unsafe) {
        return FILTER_REJECTED;
    }
    if (!bfd_copy_private_bfd_data(ib, ob)) {
        bfd_perror("Private BFD data copy failed");
        return FILTER_FAILED;
    }
    return FILTER_OK;
}

int main(int argc, char *argv[]) {
    if (bfd_init() != BFD_INIT_MAGIC) {
        fprintf(stderr, "BFD library could not initialize\n");
        return 1;
    }
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input> <output>\n", argv[0]);
        for (const char **names = bfd_target_list(); *names != NULL; names++) {
            fprintf(stderr, "Valid target: %s\n", *names);
        }
        return 1;
    }
    bfd *input = NULL, *output = NULL;
    int retcode = 1;
    input = bfd_openr(argv[1], BFD_TARGET);
    if (input == NULL) {
        bfd_perror("Could not open input");
        goto teardown;
    }
    output = bfd_openw(argv[2], BFD_TARGET);
    if (output == NULL) {
        bfd_perror("Could not open output");
        goto teardown;
    }
    enum filter_status fs = filter_elf(input, output);
    if (fs == FILTER_REJECTED) {
        fprintf(stderr, "WARNING: Cannot safely excise object %s\n", argv[1]);
        retcode = 42; /* special code indicating rejection instead of failure */
        goto teardown;
    }
    if (fs != FILTER_OK) {
        fprintf(stderr, "Failed to filter ELF file.\n");
        goto teardown;
    }
    retcode = 0; /* mark success */
teardown:
    /* must write output before we can close the input file, in case there are any references */
    if (output != NULL && !bfd_close(output)) {
        bfd_perror("Could not close output");
    }
    if (input != NULL && !bfd_close(input)) {
        bfd_perror("Could not close input");
    }
    if (retcode != 0) {
        if (unlink(argv[2]) < 0 && errno != ENOENT) {
            perror("Cannot unlink output");
        }
    }
    return retcode;
}
