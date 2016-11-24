# binscout
Search a binary file for a byte sequence.

This is reconstructed and cleaned up from various copies and variants in backups
reaching back to at least 2010. It was originally called hexcrawler and was both
simpler, and more complex. In otherwords: naive. Well at least more so.

This tool implements a feature found in any decent hex editor. The advantage
being it can be run on remote servers with only a ssh connection. Some files are
too large and distant to transfer for analysis.

There is probably an obscure command line tool, or clever invocation of a well
known one, that will provide equivalent functionality. Thanks, but I'm not
interested.

## Limitations

 - One file, one byte sequence.
 - Does not work with standard input; a cardinal sin in Unix.
 - Outputs to standard output only; something of a venial sin in Unix.
 - Warning messages will be intersprersed with output: no quiet mode.

## Missing Polish

 - Search for double and single precision IEEE 754 numbers.
 - Output offsets in a radix other than hexadecimal.
 - Process standard input by default if no file is specified.
 - Search across multiple files.
 - Start at a specified offset in the file.
 
## Missing Chrome

 - Simultaneously search for Multiple byte vectors.
 - The man page is at best only adequate.
 - Unit tests. 

## A Curious Aside on OD(1) and HEXDUMP(1)

These are ubiquitous and trusty tools. I hope this tool proves to be complimentary.

That said: by default they print out short (16 bit) integers with the most
significant byte occuring first. They do *not* print out the bytes in order.

e.g.

    $ printf "\0\1\2\3" | od -x
    0000000 0100 0302
    0000004

I have been sitting a small conference room in a far away country where I did
not speak the language. Trying to debug why the tuning configuration for a
specialized chip made made things worse. As a result of this behavior.

Happened to me -- could happen to you.
