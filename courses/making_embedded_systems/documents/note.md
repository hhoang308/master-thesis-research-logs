## C1. Introduction
- An embedded system is a computerized system that is purpose-built for its application.
- The hardware of an embedded system often has constrains, for examples, a CPU that runs slower to save battery power.
- In some systems, the software must act deterministically (act exactly the same each time) or in real time (always reacting to an event fast enough), for examples, a satellite or a heart monitor.
- Embedded systems use cross-compilers. Many larger processors use the cross-compilers from the GNU family of tools such as GCC.
- Embedded systems use cross-debugger. The debugger sits on your computer and communicates with the target processor through a special processor interface. The interface is dedicated to letting someone else eavesdrop on the processor as it works. This interface is often called JTAG.
- The processor must expend some of its resources to support the debug interface, allowing the debugger to halt it as it runs and providing the normal sorts of debug information. Supporting debugging operations adds cost to the processor.
- However, if your code is executing out of flash (or any other sort of readonly memory), instead of modifying the code, the processor has to set an internal register (hardware breakpoint) and compare it at each execution cycle to the code address being run, stopping when they match. Internal registers take up resources, too, so often there are only a limited number of hardware breakpoints available (frequently there are only two).
![computer and target processor](image.png)
- Creating a system that can be manufactured for a reasonable cost is a goal that both embedded software engineers and hardware engineers have to keep in mind.
### Principles to Confront Those Challenges
- Flexibility is not just about what the code can do right now, but also about how the code can handle its life-span.
- Using modularity, we separate the functionality into subsystems and hide the data each subsystem uses. With encapsulation, we create interfaces between the subsystems so they don’t know much about each other.
- Document what the code does, not how it does it.
- Because you have limited amount of time, implement the features, make them work, test them out, and then make them smaller or faster as needed. Looking for the bigger resource consumers after you have a working subsystem. We should forget about small efficiencies, say about 97% of the time: premature optimization is the root of all evil.
### Further Reading
- Design Patterns: Elements of Reusable Object-Oriented Software.
- Head First Design Patterns.
- Prototype to Product: A Practical Guide for Getting to Market
### Interview Question: Hello World
Question: Here is a computer with a compiler and an editor. Please implement "hello world." Once you have the basic version working, add in the functionality to get a name from the command line. Finally, tell me what happens before your code executes, in other words, before the main() function.
Hints:
- Use specific language (normally C and C++), header file to include and command argument. Have the ability to find and fix syntax error based on compiler errors.
- Mention about the program requires initialization likes settings the exception vectors to handle interrupts, init critical peripherals, init stack, init variables, calling global constructors (in C++ objects). Great if can describe what happens implicitly by the compiler and what happens explicitly (in initialization code).
- Bonus points is discussion of power-on behavior, explain why an embedded system can't be up and running 1 microsecond after the switch is flipped, understanding of power sequencing, power ramp-up time, clock stabilization time and processor reset/initialization deplay.
Answer:
- Initialize vector table for interrupt handling and reset vector.
- Initialize critical peripherals.
- Initialize stack.
- Initialize variables (initialized variables will be copied from flash to RAM, uinitialized or static variable will be assigned 0).
- Calling constructor (if the programming language is C++).
Bonus (Power-on behavior):
- Power sequencing: stablize source voltage.
- Power ramp-up time: How long does it take to get operating voltage.
- Clock stabilization:
- Processor reset/initialization delay: self reset and ready to execute the first command.

## C2. Creating a System Architecture
- Have a good view of the whole system (system architecture design) to understand solutions, hidden dependencies, write good-quality code.
- Start with OK design then improving on it before you start implement it.
- Product functions $\to$ software and hardware architecture (which hardware is necessary to support functions).
- Better way is to go from an existing product.
### Creating System Diagrams
- A series of diagrams that show the relationships between various part of the software. These will give you a view of the whole system, help you identifies bottle-neck, dependencies, new features,...
#### Context Diagram
- How the system will be used by the customer, focus on the relationship between device, users, servers, other devices, other entities.
- Help define system requirements, goals of the device.
#### Block Diagram
- How the **physical elements of the system** communicates.
- The schematics (simplified view of the hardware) + the hardware block diagram $\to$ software block diagram.
- For examples, SPI box inside the processor to show that we need to write some SPI code. If there are multiple chups connected via the same method, they should go to the same communications block in the chip.
![comparision of schematic and hardware and software block diagram](image-1.png)
- Keep the detailed pieces in a different place.
- Better to have too many boxes at this stage than too few.
#### Organigram
- Looks like an organization chart, the upper-level components tell the lower one what to do and the lower-level pieces will provide requested information and notify when errors arise. Communication shouldn't be one direction.
- **Identifies and manage dependencies and shared resources** because its increase the complexity (race condition,...).
![organizational diagram with a shared resources](image-3.png)
- To understand an existing codebases, should runs through the code in a debugger, from `main()` $\to$ interesting functions (not to get lost in the details) $\to$ again.
#### Layering Diagram
- Each object that uses something below it should touch all of the things it uses, if possible.
- Group resources if they are always used together or next to each others.
- The size of box is directly proportional to the complexity of elements.
- **Determines layers of code, interfaces and encapsulates the complexity.**
![software architecture layering diagram](image-2.png)
### Designing for Change
- Consider `what is going to change?` and what isn't before interfaces to harden the initial architecture agaist changes.
#### Encapsulate Modules
- Combining objects into hardware abstraction layer if they are unlikely to change or likely to change together.
#### Delegation of Tasks
- The diagram helps divide up and apportion work.
- If you can seperate and describe a box (or subsystem) to a person and they can work on it without knowing the whole system, that means you have a good design.
- Design defensive structure to minimize the data move between boxes to protect your code from the faulty of another one.
#### Driver Interface: Open, Close, Read, Write, IOCTL
- Top-down design: what you want $\to$ what you need.
- Bottom-up design: what you have $\to$ what you can build.
- Many drivers in embedded systems are based on the POSIX API used to call devices (access to the hardware) in UNIX systems. The interface contains: `open` (or `init`) to open driver for use, `close` to cleans up the driver after use, `read` to read data from device, `write` to sends data to the device, `ioctl` (input/output control) to handle features not coverred by the other part of the interface (monitor will have different funtions with sensor,...), `select` (or `poll`) to wait for the device to change state, `mmap` to control the memory map the driver shares with the code that calls it.
#### Adapter Pattern
- `adapter` software design pattern converts the interface of an object into one that is easier for a client to use, so that the hardware interface can change without your upper-level software changing.
    ![driver implement adapter pattern](image-4.png)
- Driver are stackable, for example, `open` for display $\to$ subsystem initialization code $\to$ `open` for the flash and `open` for SPI driver.
- Layer and adapter add delays, complexity and more memory to your code, but still a good trade with good maintainability, testing, portability.
- Testing should be considered right from the architecture design stage.
### Creating Interfaces
- Most modules will need an initialization function (though driver often use `open` for this).
- A good initialize function should be able to be called multiple times if it is used by different subsystems. A very good initialization function can reset the subsystem (or hardware resource) to a known good state in case of partial system failure.
- As you fill in the interface, just focus on whichever one is most useful to you (or clearest to your 'boss' or 'minion')
#### Example: A Logging Interface
- Specify requirements: encapsulates, handle different underlying implementations (subsystem-specific), priority-level,...
- Design interface: may changes as the interface develops, the successful criteria is "The other subsystems do not depend on the wayu XYZ is carried out, they just call the given interface".
- Version your code: should be available via the primary communication path (USB, Wifi, UART,...) and print out automatically on boot, or through a query, or compiled into objects file and located in a specific address. Notice that each piece that gets built or updated seperately should have a version that is part of the protocol.
- Object-Oriented Programming in C: used C instead of C++ for speed or match what is already there. `data hiding` in C by scoping a variable appropriately, you can mimic the idea of private variables (and even friend objects). sharing private variable (sometimes you need a backdoor to the information, consider guarding against it during normal development) by returning a pointer to the private variables.
- Keep in mind that you will also need methods for verifying your system.
### A Sandbox to Play In
- MVC (model-view-controller) is a pattern achitecture to isolate application from user interface so they can be developed and tested independently. `view` is the interface to user, both input and output. `model` is the domain-specific data and logic, takes input and creates something useful. `controller` is the glue that works between the model and the view.
- Another way to use the MVC pattern is a virtual box (or sandbox), this is a valuable way to develop and verify complex algorithm in embedded systems. Keep the `model`, switch input back and forth between a file on a PC and the actual system, redirect output to a file, changes the `controller`, so that you can do regression tests to verify algorithm.
### Back to the Drawing Board
- Should make 4 sketches of your system. Remember that change sketches much more easily than change a system source code.
- Some of the diagrams may become part of the documentation and need to be maintained.
- Making the high-level diagram too detailed is a good way to lose the whole-system picture. Sometimes a large box need to be moved to a new page for more detail.
### Further Reading
- Design Patterns: Elements of Reusable Object-Oriented Software.
- Documenting Software Architectures.
- Real-Time Software Design for Embedded Systems
### Interview Question: Create an Architecture
Q: Describe the architecture for this [pick an object in the room]
A:
The purpose of the interviewer is they want to know that candidate can break problem down into pieces, deconstruct an object, see a good overview of the whole system and also still dig deep into some areas (to know the experience of interviewee), enthusiasm for problem solving and effectively communicate their ideas.
- Choose suitable object (phones, projector,...).
- Start with inputs and outputs. Shouldn't be afraid to pick up the object and see the connections it has.
- Making connections by asking (yourself) how each component works.
- Mentioning good software design practices. Use organization diagram, encapsulate modules so that some parts of system can be reused in the future.
- Ask questions about specific features or possible design goals (because you are requested to redesign the object, so you can suggest changes in feature or design to optimize the object).
- Admit what you know and what you dont.
## C3. Getting Your Hands on the Hardware
### Hardware Design
**hardware team:** datasheet + reference designs $\to$ choose components $\to$ purchase development kits (riskiest peripheral + processor) $\to$ creates schematics $\to$ generates PDF schematics at checkpoints or review for software team $\to$ when the schematics complete, processor + peripheral in delelopment kit are suitable, the board can be laid out $\to$ the board goes to fabrication to make printed circuit board (PCB) $\to$ start assembled component froms gathering items on the BOM to make PCBA $\to$ verify power issues and other hardware subsystems.
**software team:** have development kits $\to$ finding (or building) toolchain with compiler and debugger, create debug system,... $\to$ define software to test hardware
### Board Bring-up
- when design, make each component individually testable.
- when get PCBA, start on the lowest level pieces (for example, blink led, motor moves,...) then take smallest step possible.
- make your tools independent of you being present to run them $\to$ someone can runs and reproduce the issue.
### Reading a Datasheet
- as a software engineer, consider each chip as a seperate software library $\to$ learn to familiar with processor + peripheral as hard as learning a software package (Qt, OpenCV,...), know which susbet of the documentation is important and get away with it.
- make sure you have the latest version of component and datasheet.
- reading datasheet requires experience and patience.
- the overview sections near the top of each datasheet isn't likely to be very helpful if you haven't already used a component with a datasheet that is 85% the same as this one $\to$ skip the overview header (or come back later)
- functional diagrams are a lot like the overview it is helpful if you know what you are looking for $\to$ skip functional diagram
- **start at the description**, read this section thoroughly, underline important information and try to explain it to someone else.
#### Conclusion
**read first :** description, application information (or theory of operation), timing diagram, user manual (optional), vendor code repo for example code, example codes in github or other web.
**read when debug:** pin out, pin description, performance characteristics, sample schematics (ask electrical engineer about the difference if the schematic doesn't match sample), overview.
### Datasheet Sections you need when things go wrong
- skip informations relates to hardware team (max rating, operating conditions, layout,...) come back when you have to pull out an oscilloscope. $\to$ safe to ignore on the first pass
- need to know the pinout if you have to probe the chip during debugging, the pin names with bar over them (for example, /OUT) indicate that these pins are `active low`.
### Datasheet Sections for Software Developer
- read **application information** (or **theory of operation**) from start to end, consider how you'll need to implement the driver for your system
- focus on timing diagram
- **generally:** write the driver $\to$ re-reading parts as you write the interface to the chip $\to$ communication method $\to$ actually use the chip
- when the driver working, read through the feature summary at the top of the datasheet to have better overview.
### How Timing Diagrams Help Software Developers
- timing diagram shows relationship between transition on the same or different signal.
- most diagrams focus on the digital states, showing you when a signal is high or low (check active low or active high).
- some diagrams include a ramp to show you when the signal is in a transitory state.
- signal may also both high and low, indicates the signal is in an indeterminate logical state (for output)  or isn't monitored (for input).
- focus on highlighted lines with arrow, signal ordering, causal relationship and footnotes.
### Evaluating Components Using the Datasheet
- evaluating a component is a more advanced skill than reading the datasheet (**electrical engineer generally develop before software engineer**).
- start with a list of must-haves and a list of wants -> potential pool of parts -> talking with vendor to ask guidance -> check maximum rating, electrical characteristics, typical characteristics -> selects a few parts -> read application sections, may be a good reason if a parts directedly particularly for a limited scope -> performance characteristic -> choose 2 best componets per parts -> prototype the parts with actual hardware (if possible, otherwise, estimate what will happen) -> gain enough knowledge to dealing with similar parts, so that, next time you can start with the overview and compare the difference or trade-off.
- datasheet don't have prices and lead times -> be careful.
### Your Processor is a Language
- This is the most important component.
- Some vendor (NXP, ST, Microchip) uses a common core (ARM Cortex M4F) and provide different on-chip peripherals (timer,...) -> expect differences.
- Getting to know a processor is the same with learning a new programming language. As you learn several programming language or processor, you will find that learning the new ones becomes easier and easier.
- The primary goal is to learn what you need to get things accomplished because the amount of documentation for a processor scales with its complexity.
- Useful information will be in:
    - User Manual for the processor: most of what you need to know, skip to the parts that will be on your system.
    - User Manual for the dev kit: lets you setup compiler, debugger before custom hardware comes in (give you something to compare with your hardware if it doesn't work)
    - Application Notes: describe how to accomplish different goals.
    - Errata: know what parts of the chip have errors.
    - Examples.
    