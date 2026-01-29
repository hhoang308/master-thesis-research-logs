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
- use common method `clone` for `deep copy`, which means the heap data does get copied.
- if a type implements the `Copy` trait (for example, integer, boolean, floating-point, character, or any group of simple scalar values), variables that use it do not move, but rather are trivially copied, making them still valid after assignment to another variable, because they have a known size at compile time.
- passing a variable to a function is copy or move, just as assignment does.
- examples:
	functions with ownership and scope annotated
	```
	fn main() {
		let s = String::from("hello");  // s comes into scope

		takes_ownership(s);             // s's value moves into the function...
										// ... and so is no longer valid here

		let x = 5;                      // x comes into scope

		makes_copy(x);                  // Because i32 implements the Copy trait,
										// x does NOT move into the function,
										// so it's okay to use x afterward.

	} // Here, x goes out of scope, then s. However, because s's value was moved,
	// nothing special happens.

	fn takes_ownership(some_string: String) { // some_string comes into scope
		println!("{some_string}");
	} // Here, some_string goes out of scope and `drop` is called. The backing
	// memory is freed.

	fn makes_copy(some_integer: i32) { // some_integer comes into scope
		println!("{some_integer}");
	} // Here, some_integer goes out of scope. Nothing special happens.
	```
	transferring ownership of return values
	```
	fn main() {
		let s1 = gives_ownership();        // gives_ownership moves its return
										// value into s1

		let s2 = String::from("hello");    // s2 comes into scope

		let s3 = takes_and_gives_back(s2); // s2 is moved into
										// takes_and_gives_back, which also
										// moves its return value into s3
	} // Here, s3 goes out of scope and is dropped. s2 was moved, so nothing
	// happens. s1 goes out of scope and is dropped.

	fn gives_ownership() -> String {       // gives_ownership will move its
										// return value into the function
										// that calls it

		let some_string = String::from("yours"); // some_string comes into scope

		some_string                        // some_string is returned and
										// moves out to the calling
										// function
	}

	// This function takes a String and returns a String.
	fn takes_and_gives_back(a_string: String) -> String {
		// a_string comes into
		// scope

		a_string  // a_string is returned and moves out to the calling function
	}
	```
### references and borrowing
- a `reference` is like a pointer in that is an address, but unlike a pointer, a `reference` is guaranteed to point to a valid value of a particular type for the life of that `reference`.
- `&` symbol in function definition represents references, they allow you to refer to some value without taking ownership of it, so that the value it points to will not be dropped when the reference stops being used.
- the opposite of referencing is dereferencing, which is accomplished with the dereference operator `*`.
- the action of creating a reference is `borrowing`, and we can't modify something we're borrowing.
- a `mutable reference` allow us to modify a borrowed value, and only `mut` variable can be passed to a `mutable reference`. However, `mutable reference` has a big restriction, if you have a mutable reference to a value, you can't have other reference to that value.
- we also can't have a `mutable reference` while we have an `immutable` one to the same value.
- `references` must always be valid, which means it always points to address that have value.

### slice type
- `slice` let you reference a contiguous sequence of elements in a collection, a `slice` is kind of reference, so it doesn't have ownership. for example, rather than a reference to the entire `String`, `let hello = &[0..5]` hello is a reference to a portion of the `String`. 
- `string slice` is written as `&str`.
- `string literal` is a `string slice` so that it is immutatble.


### useful funtions
- dbg!() similar to print() a variable in C.

### something
- `free memory` is to mark a location as being free, not "clear" the data in that location, because of performace.
- the concept ownership, borrowing and slices ensure memory safety at compile time.