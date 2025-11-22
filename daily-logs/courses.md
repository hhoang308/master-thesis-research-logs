# Courses

## 1. Binary Exploitation ("Pwn")
*Focus: Memory corruption, buffer overflows, heap exploitation, and shellcoding.*

### **[pwn.college](https://pwn.college/) (Highly Recommended)**
* **Provider:** Arizona State University (Yan Shoshitaishvili)
* **Cost:** Free
* **Why it's the best:** Created by the team behind the "angr" framework. It uses a "Dojo" system: you learn a concept and immediately solve a real challenge in a browser-based Linux container.
* **Key Modules:** Program Misuse, Memory Errors, Heap Exploitation, Kernel Security.

### **[RPISEC - Modern Binary Exploitation](https://github.com/RPISEC/MBE)**
* **Provider:** Rensselaer Polytechnic Institute
* **Cost:** Free
* **Why it's good:** A legendary university course. While slightly older, the theory on the Stack, Heap, and Format String vulnerabilities is timeless and essential.
* **Format:** Slides and Labs available on GitHub.

### **[Nightmare](https://guyinatuxedo.github.io/)**
* **Provider:** guyinatuxedo (Community)
* **Cost:** Free
* **Why it's good:** Functions as a "dictionary" of exploitation. If you want to learn a specific technique (e.g., "Stack Buffer Overflow with ASLR enabled"), this guide maps it to a specific CTF challenge.

## 2. Reverse Engineering (RE)
*Focus: Understanding Assembly (x86/ARM), decompilers (Ghidra/IDA), and how C++ maps to machine code.*

### **[OpenSecurityTraining2 (OST2)](https://opensecuritytraining.info/)**
* **Provider:** Xeno Kovah
* **Cost:** Free
* **Course to take:** **"Architecture 1001: x86-64 Assembly"**
* **Why it's good:** Xeno Kovah is a legend in the field. This course explains exactly how the OS loads a binary and how stack frames are built bit-by-bit. Essential for understanding *why* C++ code looks the way it does in assembly.

### **[Malware Unicorn](https://malwareunicorn.org/)**
* **Provider:** Amanda Rousseau
* **Cost:** Free
* **Course to take:** **RE101** and **RE102**
* **Why it's good:** Concise, visual workshops that focus on practical usage of assemblers and debuggers. Great for a quick start.

### **[begin.re](https://begin.re/)**
* **Provider:** Ophir Harpaz
* **Cost:** Free
* **Why it's good:** A gentle, browser-based introduction to x86 Assembly and Reverse Engineering. Good for a quick refresher.


## 3. Malware Analysis
*Focus: Dissecting malicious software, safe lab setup, static vs. dynamic analysis.*

### **[PMAT - Practical Malware Analysis & Triage](https://academy.tcm-sec.com/p/practical-malware-analysis-triage)**
* **Provider:** TCM Security (HuskyHacks)
* **Cost:** Paid (~$30 USD, often on sale)
* **Why it's good:** The current industry standard for entry-level malware analysis. It teaches you how to set up a safe lab, detonate malware, and write professional reports. Great for your CV.

### **[OALabs (Open Analysis)](https://www.youtube.com/c/OALabs)**
* **Provider:** OALabs
* **Cost:** Free (YouTube)
* **Why it's good:** Excellent tutorials on **unpacking** malware (getting past the encryption/wrappers to see the real code).

## 4. Essential YouTube Channels
* **[LiveOverflow](https://www.youtube.com/c/LiveOverflow):** The philosophy of hacking. Watch the "Binary Hacking" playlist to understand the "Why" behind bugs.
* **[StackSmashing](https://www.youtube.com/c/StackSmashing):** Embedded security and hardware hacking (IoT/Automotive focus).
* **[John Hammond](https://www.youtube.com/c/JohnHammond010):** CTF walkthroughs. Teaches the process of problem-solving.
* **[Low Level Learning](https://www.youtube.com/c/LowLevelLearning):** Great explanations of how C code interacts with hardware.

---

## Recommended Learning Path for You

1.  **Step 1:** **OST2 (Arch 1001)** - Map C++ knowledge to Assembly.
2.  **Step 2:** **pwn.college** - Learn how to break the code (Program Misuse & Memory Errors modules).
3.  **Step 3:** **PMAT (TCM Security)** - Get a "Certification" project for resume targeting Incident Response jobs.