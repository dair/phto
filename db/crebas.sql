
CREATE TABLE IF NOT EXISTS image (
    hash CHAR(32) NOT NULL,
    size BIGINT NOT NULL,

    preview_file VARCHAR(2048),
    
    PRIMARY KEY (hash, size)
);

CREATE TABLE IF NOT EXISTS file (
    filename VARCHAR(2048) NOT NULL PRIMARY KEY,
    
    hash CHAR(32) NOT NULL,
    size BIGINT NOT NULL
);

ALTER TABLE file
ADD CONSTRAINT file_image__fk FOREIGN KEY (hash, size) REFERENCES image(hash, size);


CREATE INDEX IF NOT EXISTS image_hash__idx ON image (hash);
CREATE INDEX IF NOT EXISTS image_size__idx ON image (size);


CREATE TABLE IF NOT EXISTS tag (
    id INTEGER NOT NULL PRIMARY KEY,
    name VARCHAR(1024) UNIQUE NOT NULL
);

CREATE TABLE IF NOT EXISTS image_tag (
    hash CHAR(32) NOT NULL,
    size BIGINT NOT NULL,
    
    tag_id INTEGER NOT NULL REFERENCES tag(id)
);

ALTER TABLE image_tag
ADD CONSTRAINT image_tag_image__fk FOREIGN KEY (hash, size) REFERENCES image(hash, size);

