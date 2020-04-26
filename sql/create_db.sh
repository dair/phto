#!/bin/bash
# это надо запускать из-под суперпользователя базы данных. В debian это пользователь postgres

DB_USER=phto
DB_NAME=phto

createdb -E UTF-8 -O $DB_USER $DB_NAME

