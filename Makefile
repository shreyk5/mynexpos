.PHONY: all

all:
	$(MAKE) -C expl
	$(MAKE) -C nespl
	$(MAKE) -C nexfs-interface
	$(MAKE) -C nexsm

clean:
	$(MAKE) -C expl clean
	$(MAKE) -C nespl clean
	$(MAKE) -C nexfs-interface clean
	$(MAKE) -C nexsm clean
