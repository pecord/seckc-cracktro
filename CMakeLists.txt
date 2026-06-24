cmake_minimum_required(VERSION 3.21)

project(
	rave-psx
	LANGUAGES    C CXX ASM
	VERSION      0.1.0
	DESCRIPTION  "PSX rave demoscene thing for MiSTer"
)

psn00bsdk_add_executable(rave GPREL main.c)

psn00bsdk_target_incbin(rave PRIVATE seckc_tim seckc.tim)
psn00bsdk_target_incbin(rave PRIVATE bottle_tim bottle.tim)

psn00bsdk_add_cd_image(
	iso      # CD image target
	rave     # output: rave.bin + rave.cue
	iso.xml
	DEPENDS rave system.cnf
)
