ifeq ($(CHIP_ARCH), $(filter $(CHIP_ARCH), CV181X))
    SUBDIRS := common sensor_test venc vdecvo vio sample_panel sample_ir_auto region audio ive vdec
else ifeq ($(CHIP_ARCH), $(filter $(CHIP_ARCH), CV180X))
    SUBDIRS := common sensor_test venc sample_ir_auto region audio vdec
endif

# ===== 新增：overlay 目标目录 =====
OVERLAY_DIR := ../../buildroot-2023.08.2/board/cvitek/CV181X/overlay/mnt/system/usr/bin

all:
	@echo "##############################"
	@echo "#                            #"
	@echo "#   Compiling 'samples'...   #"
	@echo "#                            #"
	@echo "##############################"
	@echo $(SUBDIRS) $(MAKECMDGOALS)

	@$(foreach t,$(SUBDIRS), \
		cd $(t) && $(MAKE) all || exit 1 && cd -; )

	@echo ">>> Copying sample binaries to overlay..."
	@mkdir -p $(OVERLAY_DIR)

	@for d in $(SUBDIRS); do \
		find $$d -type f -name "sample*" -perm -111 -exec cp -v {} $(OVERLAY_DIR)/ \; ; \
	done

	@echo ">>> Copy done."

clean:
	@$(foreach t,$(SUBDIRS), \
		cd $(t) && $(MAKE) clean || exit 1 && cd -; )
