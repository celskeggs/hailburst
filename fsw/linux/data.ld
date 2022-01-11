SECTIONS {
    .data : {
        *(.data*);
        tasktable_start = .;
        *(.tasktable);
        tasktable_end = .;
        initpoints_start = .;
        *(.initpoints);
        initpoints_end = .;
    }
}
