file (READ ${input} contents)
string (REPLACE "\n" "\\n\"\n\"" contents "${contents}")
file (WRITE ${output}
	"const char ${var_name}[] = \n\"${contents}\";\n")
