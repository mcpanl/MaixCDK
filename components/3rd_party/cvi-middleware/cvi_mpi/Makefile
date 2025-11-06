SUBDIRS := common z_mmf

all:
	@echo "##############################"
	@echo "#                            #"
	@echo "#   Compiling 'samples'...   #"
	@echo "#                            #"
	@echo "##############################"
	@echo $(SUBDIRS) $(MAKECMDGOALS)

	@$(foreach t,$(SUBDIRS), \
		cd $(t) && $(MAKE) all || exit 1 && cd -; )

clean:
	@$(foreach t,$(SUBDIRS), \
		cd $(t) && $(MAKE) clean || exit 1 && cd -; )