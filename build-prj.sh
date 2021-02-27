pushd CSpydr/src
g++ -m64 common.h main.c chunk.h chunk.c compiler.h compiler.c debug.c debug.h memory.c memory.h natives.h object.c object.h scanner.c scanner.h table.c table.h value.c value.h vm.c vm.h -o ../../bin/CSpydr
popd

#chmod +x bin/CSpydr.o