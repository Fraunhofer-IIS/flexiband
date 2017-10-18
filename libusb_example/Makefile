APPS=flexiband_fpga flexiband_record flexiband_playback

all: $(APPS)

flexiband_%: flexiband_%.c
	gcc -std=gnu99 $^ -lusb-1.0 -o $@

clean:
	rm -f $(APPS)
