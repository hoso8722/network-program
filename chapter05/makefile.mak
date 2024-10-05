# Makefileの先頭でMAKEFILE_LIST変数を定義
MAKEFILE_LIST :=$(MAKEFILE_LIST)

all:
	@echo "実行されたMakefileは$(MAKEFILE_LIST)です"