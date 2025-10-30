# Smart Greybox Fuzzing

### What

- define innovative mutation operators that work on the virtual file structure rather than on the bit level which allows SGF to explore completely new input domains while maintaining file validity.
- introduce a novel validity-based power schedule that enables SGF to spend more time generating files that are more likely to pass the parsing stage of the program.

### Why

- random bitflips to generate new files are unlikely to produce valid files (or valid chunks in files), for applications processing complex file formats.

### Result

- more branch coverage (up to 87% mprovement) than AFL.
- discovered 42 zero-day vulnerabilities in tools and libraries
- discovered 22 CVEs

### How

#### Virtual Structure

### Reproduce

### Trades-off

### Improve
