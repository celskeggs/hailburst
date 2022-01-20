SECTIONS {
    .data : {
        *(.data*);
        . = ALIGN(16);
        tasktable_start = .;
        *(tasktable);
        tasktable_end = .;
        . = ALIGN(16);
        initpoints_start = .;
        *(initpoints);
        initpoints_end = .;
    }
}
