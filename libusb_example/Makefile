
all: flexiband_record upload_fpga

flexiband_record: flexiband_record.c
	gcc -std=c99 $^ -lusb-1.0 -o $@

upload_fpga: upload_fpga.c
	gcc -std=c99 $^ -lusb-1.0 -o $@

clean:
	rm -f flexiband_record upload_fpga
