# Writing the server part for the Image/Video Organizer program

## PHASE 1: core features, offline work

C++23, capable of running on modern Linux and macOS

Designed primarily for local networks with small number of users but quite large number of Images and Videos

Estimated total Image/Video library size is about 4 terabytes, possibly up to 10. Each file is about single megabytes to maybe single-number gigabytes for longer videos.

Images are stored as files on the disk, the root directory for images are provided through the application configuration file. There should be the possibility to provide multiple root directories to store the files in several copies to prevent loss of data in case of HDD malfunction.

There is the SQLite-based database along with the files located in the place specified in the application configuration file.

The application configuration file is supposed to be read on the start of the app and its changes when the app is running are not applied before the restart.

Images are identified with the sha256 of the entire file combined with its size in bytes, this is being done to prevent duplicates.

before being stored, the image should be verified to be valid. For that, the specific libraries have been created, located in "validation" directory: "validation/jpeg" and "validation/png" for, respectively JPEG and PNG images. It is expected the number of supported formats will be increased in the newarest future so some common wrapping interface should be used for that.

The invalid image (i.e., image that failed to be validated with the appropriate validation library) should not be added and the appropriate request should return appropriate error.

The facade interface should provide functions to:

* Add image: accepts raw data as a parameter, returns either the id of the image (which is the sha256 hash of the source file), or error: broken image, duplicate image, not supported format, other errors possibly.
* Get image by id; all of the image's tags should be provided along with the image, maybe in a separate return or something
* Get list of IDs of images by given set of tags: all images should be associated with all tags provided; Pagination should be supported, i.e., the index of the starting image and number of images to return

Offer more interface functions.

## PHASE 2: network features, HTTP-accessible daemon

The program created at PHASE 1 becomes web-ready.

Network protocol is HTTP(S), Crow is the library for handling HTTP(S) requests

Secure authentication on the web today is Bearer token authentication using JWT (JSON Web Tokens), typically passed in the Authorization: Bearer <token> header. It's the backbone of most modern REST APIs and OAuth 2.0 flows. The application will use jwt-cpp library to handle the authentication.

