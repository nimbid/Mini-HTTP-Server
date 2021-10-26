# Mini-HTTP-Server

An HTTP server written in C that handles HEAD, GET, and POST requests while following some basic request headers.  

The project is a part of CSCI 5273: Network Systems.  

## Instructions
The makefile will compile the source code into a file named 'webserver'. 
```
make
```

Once the compilation is done, the executable can be run in the following manner:

### Server
```
./filepath/webserver [Port Number] 
```
*Port Number* must be greater than 5000.

**NOTE**: In the above command, 'filepath' must be replaced by the path on your system, based on your current directory. This is especially important because the server looks for files to serve based on that path.

## Authors
* Nimish Bhide

## License
[MIT](https://choosealicense.com/licenses/mit/)