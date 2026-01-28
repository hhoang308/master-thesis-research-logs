### references and mutable aliasing
- shared references (&T) like const pointer in C/C++.
	- allows to read data but not modify it.
	- can have as many shared references as you want.
- mutable references (&mut T) like non-const pointer in C/C++.
	- allows to read and modify data.
	- can have only one active mutable reference to a piece of data at a time (simultaneously).
	- a mutable reference finishes at the very last line where that specific reference is used.
- references are always valid, no dangling pointer, otherwise rust won't allow to compile the program.

### ownership
- enables Rust to make memory safety guarantees (minimize duplicate data, clean up unused data to save space) without needling a garbage collector.
- garbage collector looks for no-longer-used memory as the program runs.
- memory in Rust is managed through a system of ownership with a set of rules that the compiler checks, if any of the rules are violated, the program won't compile.
- all data stored on the stack must have a known, fixed size; in the other side, data with an unknown size at compile time or a size that might change must be stored on the heap.
- the heap is less organized than the stack, the memory allocators finds an empty spot on the heap that is big enough for the data you want to put, marks it as being in use and returns a pointer, which is the address of that location. The pointer can be stored on the stack.
- pushing to the stack is always faster than allocating on the heap because the allocator doesn't have to search for a place to store new data, same with accessing data.
- ownership rules:
	- each value in Rust has an owner.
	- there can only be one owner at a time.
	- when the owner goes out of scope, the value will be dropped.
- examples:
	- `string literal` (the value inside `""`) is hardcoded value and immutable, but `String` data types can be mutated.
	- `String` data type have to be allocated on the heap to support mutable and unknown size.
	- a `String` is made up of 3 parts: a pointer to the memory that holds the contents of the string, a length (memory that the contents are currently using) and a capacity (the total amount of memory that the `String` has received from the allocator, different from length). When assign `let s1 = s2`, the `String` data is copied, which means copy the pointer, the length and the capacity, not copy the data on the heap that the pointer refers to. So when `s1` and `s2` go out of scope, they will both try to free the same memory, which is known as double free error. To ensure memory safety, after the line `let s2 = s1`, Rust consider `s1` as no longer valid, therfore Rust doesn't need to free anything when `s1` goes out of scope, so it won't work if you try to use `s1` after `s2` is created. In Rust, we called it `move`, same meaning with `shallow copy` other languages, the concept of copying the pointer, length and capacity without copying the data.
- when you assign a completely new value to an existing variable, Rust will call `drop` and free the original values's memory immediately, at this point, nothing is refering to the original value on the heap at all.


### useful funtions
- dbg!() similar to print() a variable in C.

### something
- `free memory` is to mark a location as being free, not "clear" the data in that location, because of performace.