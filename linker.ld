OUTPUT_FORMAT("binary")
ENTRY(_start)

SECTIONS {
	. = 0x9000;

	.text : {
		*(.text)
	}

	.data ALIGN(4) : {
		*(.data)
	}

	.rodata ALIGN(4) : {
		*(.rodata)
		*(.rodata.*)
	}

	. = 0x100000;

	.bss ALIGN(4096) : {
		bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bss.*)
		*(.lbss)
		*(.lbss.*)
		. = ALIGN(4096);
		bss_end = .;
	}
}
