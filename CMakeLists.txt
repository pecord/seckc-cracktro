cmake_minimum_required(VERSION 3.21)

project(
	rave-psx
	LANGUAGES    C CXX ASM
	VERSION      0.2.0
	DESCRIPTION  "SecKC // PSX -- a PlayStation demoscene production"
)

psn00bsdk_add_executable(rave GPREL
	main.c        # entry point, boot splash, render loop
	gpu.c         # double buffer, ordering table, primitive helpers
	audio.c       # CD-DA soundtrack + VU level
	backdrop.c    # synthwave grid, sun, starfield
	logo.c        # SecKC skull slab + DVD bouncer
	text.c        # vector-font labels + scroller
	sphere.c      # environment-mapped chrome ball
)

psn00bsdk_target_incbin(rave PRIVATE seckc_tim seckc.tim)
psn00bsdk_target_incbin(rave PRIVATE dvd_tim dvd.tim)

psn00bsdk_add_cd_image(
	iso      # CD image target
	rave     # output: rave.bin + rave.cue
	iso.xml
	DEPENDS rave system.cnf
)
