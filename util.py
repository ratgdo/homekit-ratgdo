def chunker(seq, size):
    return (seq[pos:pos + size] for pos in range(0, len(seq), size))

def split_pkt(pkt):
    """
    Split a hex-encoded string into 0x-prefixed bytes suitable for including in a C file

    ```
    >>> do("5501004A2BB4FAE1A8DF759112783886AD64D5")
    0x55, 0x01, 0x00, 0x4A, 0x2B, 0xB4, 0xFA, 0xE1, 0xA8, 0xDF, 0x75, 0x91, 0x12, 0x78, 0x38, 0x86, 0xAD, 0x64, 0xD5
    ```
    """
    print(f'0x{", 0x".join(chunker(pkt, 2))}')
