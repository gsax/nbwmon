project('nbwmon', 'c',
	default_options : [ 
	  'c_std=c11',
	  'warning_level=3' ])

src = [ 'src/nbwmon.c', 'src/util.c' ]

ncursesdep = dependency('ncurses')

executable('nbwmon',
	   sources : src,
	   dependencies : ncursesdep)

run_target('inspector',
	   command : 'scripts/inspect.sh')
