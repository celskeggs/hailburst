package readlog

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"log"
	"math"
	"strconv"
	"strings"
)

type ArgType uint8

const (
	ArgNone ArgType = iota
	ArgChar         // char to be printed as a char
	ArgByte         // char-sized integer
	ArgShort
	ArgInt
	ArgLong
	ArgLongLong
	ArgPtrDiffT
	ArgIntMaxT
	ArgSizeT
	ArgVoidPtr
	ArgDouble
	ArgString
)

func (at ArgType) Size() int {
	switch at {
	case ArgChar:
		return 1
	case ArgByte:
		return 1
	case ArgShort:
		return 2
	case ArgInt:
		return 4
	case ArgLong:
		return 4
	case ArgLongLong:
		return 8
	case ArgPtrDiffT:
		return 4
	case ArgIntMaxT:
		return 8
	case ArgSizeT:
		return 4
	case ArgVoidPtr:
		return 4
	case ArgDouble:
		return 8
	case ArgString:
		// because nothing is stored inline
		return 0
	default:
		log.Panicf("invalid argument type: %v", at)
		return -1
	}
}

func (at ArgType) IsStoredExtended() bool {
	return at == ArgString
}

func (at ArgType) Decode(record []byte) interface{} {
	switch at {
	case ArgChar:
		return record[0]
	case ArgByte:
		return record[0]
	case ArgShort:
		return binary.LittleEndian.Uint16(record)
	case ArgInt:
		return binary.LittleEndian.Uint32(record)
	case ArgLong:
		return binary.LittleEndian.Uint32(record)
	case ArgLongLong:
		return binary.LittleEndian.Uint64(record)
	case ArgPtrDiffT:
		return binary.LittleEndian.Uint32(record)
	case ArgIntMaxT:
		return binary.LittleEndian.Uint64(record)
	case ArgSizeT:
		return binary.LittleEndian.Uint32(record)
	case ArgVoidPtr:
		return binary.LittleEndian.Uint32(record)
	case ArgDouble:
		return math.Float64frombits(binary.LittleEndian.Uint64(record))
	case ArgString:
		log.Panicf("attempt to decode string from non-extended storage")
		return nil
	default:
		log.Panicf("invalid argument type: %v", at)
		return nil
	}
}

func (at ArgType) AsUint64(v interface{}) uint64 {
	switch at {
	case ArgByte:
		return uint64(v.(uint8))
	case ArgShort:
		return uint64(v.(uint16))
	case ArgInt:
		return uint64(v.(uint32))
	case ArgLong:
		return uint64(v.(uint32))
	case ArgLongLong:
		return v.(uint64)
	case ArgPtrDiffT:
		return uint64(v.(uint32))
	case ArgIntMaxT:
		return v.(uint64)
	case ArgSizeT:
		return uint64(v.(uint32))
	case ArgVoidPtr:
		return uint64(v.(uint32))
	default:
		log.Panicf("invalid argument type: %v", at)
		return 0
	}
}

func (at ArgType) AsInt64(v interface{}) int64 {
	switch at {
	case ArgByte:
		return int64(int8(v.(uint8)))
	case ArgShort:
		return int64(int16(v.(uint16)))
	case ArgInt:
		return int64(int32(v.(uint32)))
	case ArgLong:
		return int64(int32(v.(uint32)))
	case ArgLongLong:
		return int64(v.(uint64))
	case ArgPtrDiffT:
		return int64(int32(v.(uint32)))
	case ArgIntMaxT:
		return int64(v.(uint64))
	case ArgSizeT:
		return int64(int32(v.(uint32)))
	case ArgVoidPtr:
		return int64(int32(v.(uint32)))
	default:
		log.Panicf("invalid argument type: %v", at)
		return 0
	}
}

func (at ArgType) DecodeExtendedStorage(data []byte) (consumed int, isLast bool, result interface{}) {
	if at != ArgString {
		panic("invalid arg type for decoding extended storage")
	}
	// find null terminator
	index := bytes.IndexByte(data, 0)
	if index == -1 {
		// no null terminator, remaining must be a single string
		return len(data), true, string(data)
	} else {
		return index + 1, false, string(data[:index])
	}
}

const (
	VariableWidthOrPrecision = -1
)

type Specifier struct {
	Type              ArgType
	Base              int
	Width             int
	Precision         int
	Signed            bool
	FlagZeropad       bool
	FlagLeft          bool
	FlagPlus          bool
	FlagSpace         bool
	FlagHash          bool
	FlagPrecision     bool
	FlagUppercase     bool
	FlagExponential   bool
	FlagAdaptExponent bool
}

func (s Specifier) Render(i interface{}) (block string) {
	if s.Type == ArgString {
		block = i.(string)
		if s.FlagPrecision && len(block) > s.Precision {
			block = block[:s.Precision]
		}
	} else if s.Type == ArgChar {
		block = string([]byte{i.(uint8)})
	} else if s.Type == ArgDouble {
		value := i.(float64)
		precision := -1
		if s.FlagPrecision {
			precision = s.Precision
		}
		var mode byte = 'f'
		if s.FlagExponential {
			mode = 'e'
			if s.FlagAdaptExponent {
				mode = 'g'
			}
		}
		if s.FlagUppercase {
			mode = mode - 'f' + 'F'
		}
		block = strconv.FormatFloat(value, mode, precision, 64)
	} else {
		// these changes are made locally only, because 's' is not a pointer
		if s.Base == 10 {
			s.FlagHash = false
		}
		if s.Signed {
			s.FlagPlus = false
			s.FlagSpace = false
		}
		if s.FlagPrecision {
			s.FlagZeropad = false
		}
		var negative bool
		var uValue uint64
		if s.Signed {
			value := s.Type.AsInt64(i)
			if value < 0 {
				negative = true
				uValue = uint64(0 - value)
			} else {
				uValue = uint64(value)
			}
		} else {
			uValue = s.Type.AsUint64(i)
		}
		if uValue == 0 {
			s.FlagHash = false
		}
		block = strconv.FormatUint(uValue, s.Base)
		if !s.FlagLeft {
			if s.Width > 0 && s.FlagZeropad && (negative || s.FlagPlus || s.FlagSpace) {
				s.Width -= 1
			}
			if len(block) < s.Precision {
				block = strings.Repeat("0", s.Precision-len(block)) + block
			}
			if s.FlagZeropad && len(block) < s.Width {
				block = strings.Repeat("0", s.Width-len(block)) + block
			}
		}
		if s.FlagHash {
			if !s.FlagPrecision && len(block) > 0 && (len(block) == s.Precision || len(block) == s.Width) {
				block = block[:len(block)-1]
				if len(block) > 0 && s.Base == 16 {
					block = block[:len(block)-1]
				}
			}
			if s.Base == 16 {
				block = "0x" + block
			} else if s.Base == 2 {
				block = "0b" + block
			} else {
				block = "0" + block
			}
		}
		if negative {
			block = "-" + block
		} else if s.FlagPlus {
			block = "+" + block
		} else if s.FlagSpace {
			block = " " + block
		}
		if s.FlagUppercase {
			block = strings.ToUpper(block)
		}
	}
	if !s.FlagLeft && len(block) < s.Width {
		return strings.Repeat(" ", s.Width-len(block)) + block
	} else if s.FlagLeft && len(block) < s.Width {
		return block + strings.Repeat(" ", s.Width-len(block))
	} else {
		return block
	}
}

type Formatter struct {
	textSegments []string
	specifiers   []Specifier
}

func (f *Formatter) ArgTypes() (ats []ArgType) {
	for _, spec := range f.specifiers {
		if spec.Width == VariableWidthOrPrecision {
			ats = append(ats, ArgInt)
		}
		if spec.Precision == VariableWidthOrPrecision {
			ats = append(ats, ArgInt)
		}
		ats = append(ats, spec.Type)
	}
	return ats
}

func (f *Formatter) Render(arguments []interface{}) string {
	if len(f.textSegments) != len(f.specifiers)+1 {
		panic("invalid formatter structure")
	}
	fragments := make([]string, len(f.textSegments)*2-1)
	for i, segment := range f.textSegments {
		fragments[2*i] = segment
	}
	nextArg := 0
	for i, spec := range f.specifiers {
		var localSpec Specifier = spec
		if localSpec.Width == VariableWidthOrPrecision {
			localSpec.Width = int(arguments[nextArg].(uint32))
			if localSpec.Width < 0 {
				localSpec.FlagLeft = true
				localSpec.Width = -localSpec.Width
			}
			nextArg += 1
		}
		if localSpec.Precision == VariableWidthOrPrecision {
			localSpec.Precision = int(arguments[nextArg].(uint32))
			if localSpec.Precision < 0 {
				localSpec.Precision = 0
			}
			nextArg += 1
		}
		fragments[2*i+1] = spec.Render(arguments[nextArg])
		nextArg += 1
	}
	return strings.Join(fragments, "")
}

type stringParser struct {
	format string
	index  int
}

func newStringParser(format string) stringParser {
	return stringParser{
		format: format,
		index:  0,
	}
}

func (sp *stringParser) IsEmpty() bool {
	return sp.index >= len(sp.format)
}

func (sp *stringParser) Peek() byte {
	if sp.IsEmpty() {
		return 0
	} else {
		return sp.format[sp.index]
	}
}

func (sp *stringParser) Advance() {
	if sp.IsEmpty() {
		panic("invalid state")
	}
	sp.index += 1
}

func (sp *stringParser) Take() byte {
	c := sp.Peek()
	sp.Advance()
	return c
}

func (sp *stringParser) Accept(c byte) bool {
	if sp.Peek() == c {
		sp.Advance()
		return true
	}
	return false
}

func (sp *stringParser) Uint() (value int, ok bool) {
	for sp.Peek() >= '0' && sp.Peek() <= '9' {
		value = value*10 + int(sp.Take()-'0')
		ok = true
	}
	return value, ok
}

func ParseFormat(format string) (Formatter, error) {
	// based on embedded-artistry printf format
	sp := newStringParser(format)
	textSegments := [][]byte{nil}
	var specifiers []Specifier
	for !sp.IsEmpty() {
		if !sp.Accept('%') || sp.Peek() == '%' {
			// regular text OR a %% escape
			idx := len(textSegments) - 1
			textSegments[idx] = append(textSegments[idx], sp.Take())
			continue
		}
		var spec Specifier
	parseFlags:
		for {
			switch sp.Peek() {
			case '0':
				spec.FlagZeropad = true
			case '-':
				spec.FlagLeft = true
			case '+':
				spec.FlagPlus = true
			case ' ':
				spec.FlagSpace = true
			case '#':
				spec.FlagHash = true
			default:
				break parseFlags
			}
			sp.Advance()
		}
		// parse width
		if sp.Accept('*') {
			spec.Width = VariableWidthOrPrecision
		} else if value, ok := sp.Uint(); ok {
			spec.Width = value
		}
		// parse precision
		if sp.Accept('.') {
			spec.FlagPrecision = true
			if sp.Accept('*') {
				spec.Precision = VariableWidthOrPrecision
			} else if value, ok := sp.Uint(); ok {
				spec.Precision = value
			}
		}
		// parse data type
		switch {
		case sp.Accept('l'):
			if sp.Accept('l') {
				spec.Type = ArgLongLong
			} else {
				spec.Type = ArgLong
			}
		case sp.Accept('h'):
			if sp.Accept('h') {
				spec.Type = ArgByte
			} else {
				spec.Type = ArgShort
			}
		case sp.Accept('t'):
			spec.Type = ArgPtrDiffT
		case sp.Accept('j'):
			spec.Type = ArgIntMaxT
		case sp.Accept('z'):
			spec.Type = ArgSizeT
		default:
			spec.Type = ArgInt
		}
		if sp.IsEmpty() {
			return Formatter{}, fmt.Errorf("unexpected end of format string")
		}
		// parse main specifier
		sc := sp.Take()
		switch sc {
		// use spec.Type from above for the integer formats
		case 'd':
			spec.Base = 10
			spec.Signed = true
		case 'i':
			// identical to %d for printf (not scanf)
			spec.Base = 10
			spec.Signed = true
		case 'u':
			spec.Base = 10
		case 'x':
			spec.Base = 16
		case 'X':
			spec.FlagUppercase = true
			spec.Base = 16
		case 'o':
			spec.Base = 8
		case 'b':
			spec.Base = 2
		case 'f':
			spec.Type = ArgDouble
		case 'F':
			spec.FlagUppercase = true
			spec.Type = ArgDouble
		case 'e':
			spec.FlagExponential = true
			spec.Type = ArgDouble
		case 'E':
			spec.FlagExponential = true
			spec.FlagUppercase = true
			spec.Type = ArgDouble
		case 'g':
			spec.FlagExponential = true
			spec.FlagAdaptExponent = true
			spec.Type = ArgDouble
		case 'G':
			spec.FlagExponential = true
			spec.FlagAdaptExponent = true
			spec.FlagUppercase = true
			spec.Type = ArgDouble
		case 'c':
			spec.Type = ArgChar
		case 's':
			// TODO: have the preprocessor actually cut off strlen at the appropriate length, if a precision is specified
			spec.Type = ArgString
		case 'p':
			spec.Type = ArgVoidPtr
			spec.Width = ArgVoidPtr.Size() * 2
			spec.Base = 16
			spec.FlagZeropad = true
			spec.FlagUppercase = true
		default:
			return Formatter{}, fmt.Errorf("unrecognized format specifier %v", rune(sc))
		}
		specifiers = append(specifiers, spec)
		textSegments = append(textSegments, nil)
	}
	f := Formatter{
		specifiers: specifiers,
	}
	for _, ts := range textSegments {
		f.textSegments = append(f.textSegments, string(ts))
	}
	return f, nil
}
