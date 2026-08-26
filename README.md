<p align="center">
  <img src="https://github.com/user-attachments/assets/2e04d1d6-52d9-4590-96be-290de5e850e3" alt="NSC" />
</p>

**Networking System C (NSC)** is a lightweight, cross-platform library that simplifies socket usage with tools for server/client handling, an event system, and message framing over TCP.

## Table of Contents
- [History](#history)
- [Features](#features)
- [Usage Example](#usage-example)
- [Tests](#tests)
- [Contributing](#contributing)

## History
This is the third version of the library, first created in **July 2024**.  
It’s not thoroughly documented, as I mainly built it as a support tool for my other project, [NSCpp](https://github.com/Tom-KB/NSCpp).  
Feel free to use it if you find it useful. It may receive occasional updates, but mainly for **bug fixes**—no new features are planned.

## Features
NSC is designed to simplify server/client creation and management.  
Here’s a list of features provided by the library:
 - **IPv4** and **IPv6** support
 - **TCP** and **UDP** communications protocols
 - ***Message framing*** for TCP
 - **Domain Name resolution**
 - An **event** system
 - **Windows** and **Linux** support
 - Messages up to `MaxMessageSize` (16 MiB by default), the read buffer grows on demand

## Usage example
You can find a *basic* chat application example—supporting **TCP** or **UDP** and **IPv4** or **IPv6**—in the [examples folder](examples/).  
You can also find a `.a` and `.lib` of the last version of NSC inside the [static library's folder](static-library/).

## Tests
The framing tests live in the [tests folder](tests/).  
Run `compileTests.bat` on Windows or `compileTests.sh` on Linux.  
They cover message sizes around the read chunk and well beyond it, several messages arriving in a single read, messages split across several reads, and invalid announced lengths.

## Contributing
Contributions are welcome.  
Feel free to improve this project by fixing or reporting any bugs you find.
