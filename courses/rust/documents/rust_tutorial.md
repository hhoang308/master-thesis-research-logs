shared references (&T) like const pointer in C/C++
	- allows to read data but not modify it 
	- can have as many shared references as you want
mutable references (&mut T) like non-const pointer in C/C++
	- allows to read and modify data
	- can have only one active mutable reference to a piece of data at a time (simultaneously)
	- a mutable reference finishes at the very last line where that specific reference is used