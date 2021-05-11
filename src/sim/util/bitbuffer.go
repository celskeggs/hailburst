package util

type BitQueue struct {
	data     []bool
	capacity int
}

func MakeBitBuffer(capacity int) *BitQueue {
	if capacity < 1 {
		panic("capacity too small to be valid")
	}
	return &BitQueue{
		data:     make([]bool, 0, capacity),
		capacity: capacity,
	}
}

func (bb *BitQueue) Put(data []bool) int {
	count := bb.capacity - len(bb.data)
	if count > len(data) {
		count = len(data)
	}
	if count < 0 {
		panic("invalid capacity")
	}
	if count > 0 {
		bb.data = append(bb.data, data[:count]...)
	}
	return count
}

func (bb *BitQueue) Take(out []bool) int {
	count := copy(out, bb.data)
	if count > 0 {
		bb.data = bb.data[count:]
	}
	return count
}

func (bb *BitQueue) Peek(out []bool) int {
	return copy(out, bb.data)
}

func (bb *BitQueue) Skip(count int) int {
	if count > len(bb.data) {
		count = len(bb.data)
	}
	if count > 0 {
		bb.data = bb.data[count:]
	}
	return count
}

func (bb *BitQueue) BitCount() int {
	return len(bb.data)
}

func (bb *BitQueue) SpaceCount() int {
	return bb.capacity - len(bb.data)
}

/*
SCRAP CODE TO COME BACK TO IF I NEED BETTER PERFORMANCE

const bitsPerU64 = 64

type bitbuffer struct {
	storage []uint64
	// all three of these are measured in bits
	capacity int
	readindex int
	writeindex int
}

func (bb *bitbuffer) peekBits(max int) (bits uint64, numBits int) {
	cellOffset, bitOffset := bb.readindex / bitsPerU64, bb.readindex % bitsPerU64
	available := bitsPerU64 - bitOffset
	if available > bb.writeindex - bb.readindex {
		available = bb.writeindex - bb.readindex
	}
	if available > max {
		available = max
	}
	if available < 0 || available > bitsPerU64 {
		panic("available bit # out of valid range")
	}
	if available == 0 {
		return 0, 0
	}
	cell := bb.storage[cellOffset]
	cell >>= bitOffset
	cell &= (1 << available) - 1
	return cell, available
}

func (bb *bitbuffer) ReadBits(count int) (uint64, bool) {
	if count < 1 || count > bitsPerU64 {
		panic("invalid number of bits to ReadBits")
	}
	if bb.writeindex - bb.readindex < count {
		return 0, false
	}
	data1, data1Len := bb.peekBits(count)
	if data1Len <= 0 {
		panic("should be at least one bit in current word")
	}
	// advance read pointer
	bb.readindex += data1Len

	// if the count is large enough, we may need to read from a second cell
	data2, data2Len := bb.peekBits(count - data1Len)
	if data1Len + data2Len != count || data2Len < 0 {
		panic("invalid lengths of bits")
	}
	// advance read pointer
	bb.readindex += data2Len

	if bb.readindex > bb.writeindex {
		panic("invalid updated read index")
	}

	return data1 | (data2 << data1Len), true
}
*/
