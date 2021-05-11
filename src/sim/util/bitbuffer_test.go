package util

import (
	"math/rand"
	"testing"
)

func TestBitBuffer(t *testing.T) {
	allInputBits := make([]bool, 64*1024)
	rand.Seed(789)
	oneBits := 0
	for i := 0; i < len(allInputBits); i++ {
		allInputBits[i] = rand.Intn(2) == 1
		if allInputBits[i] {
			oneBits += 1
		}
	}
	t.Logf("Number of one bits in input: %d\n", oneBits)

	capacity := 333
	currentLen := 0
	bb := MakeBitBuffer(capacity)
	var outputBits []bool

	inputOffset := 0
	for len(outputBits) < len(allInputBits) {
		if rand.Intn(10) >= 4 && inputOffset < len(allInputBits) {
			// perform insertion
			numToInsert := rand.Intn(capacity * 2)
			if numToInsert > len(allInputBits)-inputOffset {
				numToInsert = len(allInputBits) - inputOffset
			}
			inputData := append([]bool{}, allInputBits[inputOffset:][:numToInsert]...)
			if len(inputData) != numToInsert {
				t.Fatal("mismatched number of bits to insert")
			}

			expectedInserted := numToInsert
			if expectedInserted > capacity-currentLen {
				expectedInserted = capacity - currentLen
			}
			actuallyInserted := bb.Put(inputData)
			// t.Logf("Inserted %d/%d bits into buffer", actuallyInserted, len(inputData))
			if actuallyInserted < 0 || actuallyInserted > capacity {
				t.Fatal("impossible insertion count")
			}
			if actuallyInserted != expectedInserted {
				t.Fatalf("incorrect insertion count: %d instead of %d (nti=%d, cap=%d, cur=%d)", actuallyInserted, expectedInserted, numToInsert, capacity, currentLen)
			}
			inputOffset += actuallyInserted
			currentLen += actuallyInserted
		}
		if rand.Intn(10) >= 4 {
			// perform extraction
			numToExtract := rand.Intn(capacity * 2)
			outputData := make([]bool, numToExtract)
			replicaData := make([]bool, numToExtract)

			expectedExtracted := numToExtract
			if expectedExtracted > currentLen {
				expectedExtracted = currentLen
			}
			actuallyPeeked := bb.Peek(replicaData)
			var actuallyExtracted int
			if rand.Intn(10) >= 8 {
				// just use peek+skip instead of take
				actuallyExtracted = bb.Skip(actuallyPeeked)
				copy(outputData, replicaData)
			} else {
				actuallyExtracted = bb.Take(outputData)
			}
			// t.Logf("Extracted %d/%d bits into buffer", actuallyExtracted, len(outputData))
			if actuallyExtracted < 0 || actuallyExtracted > capacity {
				t.Fatal("impossible extraction count")
			}
			if actuallyExtracted != expectedExtracted {
				t.Fatal("incorrect extraction count")
			}
			if actuallyExtracted != actuallyPeeked {
				t.Fatal("incorrect peek count")
			}
			for i := 0; i < actuallyPeeked; i++ {
				if outputData[i] != replicaData[i] {
					t.Fatal("mismatched between peek and actual")
				}
			}
			outputBits = append(outputBits, outputData[:actuallyExtracted]...)
			currentLen -= actuallyExtracted
		}
		if currentLen+len(outputBits) != inputOffset {
			t.Fatal("incorrect matching of lengths")
		}
		if bb.BitCount() != currentLen {
			t.Fatal("incorrect bit count")
		}
		if bb.SpaceCount() != capacity-currentLen {
			t.Fatal("incorrect space count")
		}
	}

	if currentLen != 0 {
		t.Fatal("expected to be empty")
	}
	checkOut := make([]bool, 1)
	if bb.Take(checkOut) != 0 {
		t.Fatal("unexpected final extraction check")
	}
	if len(outputBits) != len(allInputBits) {
		t.Fatal("incorrect number of output bits")
	}
	for i := 0; i < len(allInputBits); i++ {
		if outputBits[i] != allInputBits[i] {
			t.Fatal("mismatched output bit")
		}
	}
	oneBitsFinal := 0
	for _, bit := range outputBits {
		if bit {
			oneBitsFinal++
		}
	}

	t.Logf("Number of one bits in output: %d\n", oneBitsFinal)
	if oneBitsFinal != oneBits {
		t.Fatal("mismatched number of one bits")
	}
}
