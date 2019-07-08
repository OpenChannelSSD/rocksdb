BUILD_TYPE?=Release
BUILD_DIR?=build

HASH?="none"

default: configure make

debug:
	$(eval BUILD_TYPE := Debug)

cmake_check:
	@cmake --version || (echo "\n** Please install 'cmake' **\n" && exit 1)

configure: cmake_check
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) ..
	@echo "Modify build configuration in '$(BUILD_DIR)'"

make:
	cd $(BUILD_DIR) && make -j$(expr `nproc` - 1)

install:
	cd $(BUILD_DIR) && make install

clean:
	rm -r $(BUILD_DIR) || true
	rm tags || true

all: clean default install

.PHONY: tags
tags:
	ctags * -R .
	ctags -R --c++-kinds=+p --fields=+iaS --extra=+q ../

dev: clean configure make

rocks:
	cd .. && make release -j$(expr `nproc` - 2)
	#cd .. && make release -j8

.PHONY: hash
hash:
	@$(eval HASH := $(shell date | md5sum | cut -b -8))
	@true

test-clean:
	@sudo rm -r /opt/rocks/test || true
	@sudo mkdir -p /opt/rocks/test || true
	@sudo chown -R `whoami`:`whoami` /opt/rocks || true
	@rm /tmp/test-*-*.log || true

test-01-00:
	./$(BUILD_DIR)/env_nvm_t01 0 2>&1 | tee -a /tmp/test-01-00-$(HASH).log
	cat /opt/rocks/nvm.meta
	cat /opt/rocks/test/testfile.log.meta
	nvm_addr from_hex /dev/nvme0n1 `tail -n +3 /opt/rocks/test/testfile.log.meta | cut -f 1 -d " "`

test-01-01:
	./$(BUILD_DIR)/env_nvm_t01 1 2>&1 | tee -a /tmp/test-01-01-$(HASH).log
	cat /opt/rocks/nvm.meta
	cat /opt/rocks/test/testfile.log.meta
	nvm_addr from_hex /dev/nvme0n1 `tail -n +3 /opt/rocks/test/testfile.log.meta | cut -f 1 -d " "`

# nruns nkvs do_delete disableWAL
test-02-00:
	./$(BUILD_DIR)/env_nvm_t02 3 50000 0 1 2>&1 | tee -a /tmp/test-02-00-$(HASH).log

test-02-01:
	./$(BUILD_DIR)/env_nvm_t02 3 50000 1 1 2>&1 | tee -a /tmp/test-02-01-$(HASH).log

bench-clean:
	@sudo rm -r /opt/rocks/hw || true
	@sudo rm -r /opt/rocks/fs || true
	@sudo rm -r /opt/rocks/qe || true
	@sudo mkdir -p /opt/rocks/hw || true
	@sudo mkdir -p /opt/rocks/fs || true
	@sudo mkdir -p /opt/rocks/qe || true
	@sudo chown -R `whoami`:`whoami` /opt/rocks || true
	@rm /tmp/bench*.log || true

bench-nvme0-fill:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_nvm_nvme0.sh fillseq 0 2>&1 | tee -a /tmp/bench-nvme0-fill-$(HASH).log

bench-nvme0-read:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_nvm_nvme0.sh readseq 1 2>&1 | tee -a /tmp/bench-nvme0-read-$(HASH).log

bench-nvme1-fill:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_nvm_nvme1.sh fillseq 0 2>&1 | tee -a /tmp/bench-nvme1-fill-$(HASH).log

bench-nvme1-read:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_nvm_nvme1.sh readseq 1 2>&1 | tee -a /tmp/bench-nvme1-read-$(HASH).log

bench-fs_ss-fill:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_fs_ss.sh fillseq 0 2>&1 | tee -a /tmp/bench-fs_ss-fill-$(HASH).log

bench-fs_ss-read:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_fs_ss.sh readseq 1 2>&1 | tee -a /tmp/bench-fs_ss-read-$(HASH).log

bench-fs_pblk-fill:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_fs_pblk.sh fillseq 0 2>&1 | tee -a /tmp/bench-fs_pblk-fill-$(HASH).log

bench-fs_pblk-read:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_fs_pblk.sh readseq 1 2>&1 | tee -a /tmp/bench-fs_pblk-read-$(HASH).log

bench-qe-fill:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_qemu.sh fillseq 0 2>&1 | tee -a /tmp/bench-qe-fill-$(HASH).log

bench-qe-read:
	./scripts/run.sh ./scripts/env_shared.sh ./scripts/env_qemu.sh readseq 1 2>&1 | tee -a /tmp/bench-qe-read-$(HASH).log

