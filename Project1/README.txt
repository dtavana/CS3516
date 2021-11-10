Start server: ./server -p PORT -r NUM_REQUESTS -s NUM_SECONDS -m MAX_USERS
Start client: ./client HOST FILENAME PORT

NOTES: 
- The server does not include timeout functionality
    - I did not understand until the last minute that the client should keep the connection open for use several times. As a result, I could not implement a timeout.
        - The client has a socket created for one time use and then it is closed
- All other functionality should be present.
- Testfiles
    - test.png is a valid QR Code
    - test2.png is an invalid QR Code
    - test3.png is an image that is too large to process
- socketserver.log is present in the out/ directory