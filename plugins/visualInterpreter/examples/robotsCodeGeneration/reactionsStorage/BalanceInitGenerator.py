import os

balanceInitCode = "balance_init();"

scriptDir = os.path.dirname(__file__)
relPath = "nxt-tools/program0.c"
absPath = os.path.join(scriptDir, relPath)

codeFile = open(absPath, 'r')
code = codeFile.read()

code = code.replace("@@CODE@@", balanceInitCode + "@@CODE@@")

codeFile.close()
codeFile = open(absPath, 'w')
codeFile.write(code)
codeFile.close()