package readlog

import (
	"debug/elf"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"io"
	"os"
	"strings"
)

const (
	JunkDataUID      = 0x00000001 // special MessageUID just for junk data indications
	InvalidRecordUID = 0x00000002 // special MessageUID for records that could not be parsed properly
)

type LogLevel int
const (
	LogInvalid  LogLevel = 0
	LogCritical LogLevel = 1
	LogInfo     LogLevel = 2
	LogDebug    LogLevel = 3
	LogTrace    LogLevel = 4
)

func LogLevels() []LogLevel {
	return []LogLevel{LogCritical, LogInfo, LogDebug, LogTrace}
}

func ParseLogLevel(raw uint32) (LogLevel, error) {
	if raw >= 1 && raw <= 4 {
		return LogLevel(raw), nil
	} else {
		return LogInvalid, fmt.Errorf("invalid log level: %d", raw)
	}
}

func ParseStringLogLevel(raw string) (LogLevel, error) {
	for _, level := range LogLevels() {
		if strings.EqualFold(level.String(), raw) {
			return level, nil
		}
	}
	return LogInvalid, fmt.Errorf("invalid log level: %q", raw)
}

func (ll LogLevel) String() string {
	switch ll {
	case LogCritical:
		return "CRIT"
	case LogInfo:
		return "INFO"
	case LogDebug:
		return "DEBUG"
	case LogTrace:
		return "TRACE"
	default:
		panic("invalid log level")
	}
}

type MessageMetadata struct {
	MessageUID uint32
	LogLevel   LogLevel
	Format     Formatter
	Filename   string
	LineNum    uint32
}

func (m *MessageMetadata) ParseRecord(record []byte) ([]interface{}, error) {
	argTypes := m.Format.ArgTypes()
	totalLength := 0
	countExtended := 0
	for _, at := range argTypes {
		if at.IsStoredExtended() {
			if at.Size() != 0 {
				panic("invalid size setting for extended storage value")
			}
			countExtended += 1
		} else {
			totalLength += at.Size()
		}
	}
	if len(record) < totalLength {
		return nil, fmt.Errorf("record for uid=0x%08x is too short: %d bytes when minimum was %d",
			m.MessageUID, len(record), totalLength)
	}
	if len(record) > totalLength && countExtended == 0 {
		return nil, fmt.Errorf("record for uid=0x%08x is too long: %d bytes when maximum was %d",
			m.MessageUID, len(record), totalLength)
	}
	arguments := make([]interface{}, len(argTypes))
	// parse regular data first
	offset := 0
	for i, argType := range argTypes {
		if !argType.IsStoredExtended() {
			size := argType.Size()
			arguments[i] = argType.Decode(record[offset:][:size])
			offset += size
		}
	}
	if offset != totalLength {
		panic("inconsistent")
	}
	extIndex := 0
	// parse extended storage data next
	for i, argType := range argTypes {
		if argType.IsStoredExtended() {
			extIndex += 1
			consumed, isLast, object := argType.DecodeExtendedStorage(record[offset:])
			if isLast && extIndex != countExtended {
				return nil, fmt.Errorf("out of data for parsing extended storage values")
			} else if !isLast && extIndex == countExtended {
				return nil, fmt.Errorf("unexpected extra data while parsing extended storage values")
			}
			arguments[i] = object
			offset += consumed
		}
	}
	return arguments, nil
}

type Record struct {
	Metadata     *MessageMetadata
	ArgumentData []interface{}
	Timestamp    model.VirtualTime
}

type DebugFile struct {
	file *os.File
	elf  *elf.File
	syms []elf.Symbol
}

func LoadDebugFile(path string) (*DebugFile, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	elfFile, err := elf.NewFile(file)
	if err != nil {
		_ = file.Close()
		return nil, err
	}
	symbols, err := elfFile.Symbols()
	if err != nil {
		_ = file.Close()
		return nil, err
	}
	return &DebugFile{
		file: file,
		elf:  elfFile,
		syms: symbols,
	}, nil
}

func (f *DebugFile) findSymbols(address uint64) (s []elf.Symbol) {
	for _, sym := range f.syms {
		if sym.Value == address && sym.Size > 0 {
			s = append(s, sym)
		}
	}
	return s
}

func (f *DebugFile) ReadSymbolInto(sym elf.Symbol, expectedSection string, output interface{}) error {
	if sym.Size != uint64(binary.Size(output)) {
		return fmt.Errorf("symbol too small: %v < %v", sym.Size, binary.Size(output))
	}
	section := f.elf.Sections[sym.Section]
	if section.Name != expectedSection {
		return fmt.Errorf("incorrect section: %q instead of %q", section.Name, expectedSection)
	}
	reader := section.Open()
	offset := sym.Value - section.Addr
	if offset < 0 || offset >= section.Size || offset+sym.Size > section.Size {
		return fmt.Errorf("section symbol lookup is out of range")
	}
	if _, err := reader.Seek(int64(offset), io.SeekStart); err != nil {
		return err
	}
	return binary.Read(reader, binary.LittleEndian, output)
}

func (f *DebugFile) ReadCStringFrom(sym elf.Symbol, expectedSection string) (string, error) {
	if sym.Size < 1 {
		return "", fmt.Errorf("symbol too small: %v", sym.Size)
	}
	section := f.elf.Sections[sym.Section]
	if section.Name != expectedSection {
		return "", fmt.Errorf("incorrect section: %q instead of %q", section.Name, expectedSection)
	}
	reader := section.Open()
	offset := sym.Value - section.Addr
	if offset < 0 || offset >= section.Size {
		return "", fmt.Errorf("section symbol lookup is out of range")
	}
	_, err := reader.Seek(int64(offset), io.SeekCurrent)
	if err != nil {
		return "", fmt.Errorf("")
	}
	var b [1]byte
	var str []byte
	for {
		n, err := reader.Read(b[:])
		if err != nil {
			if err == io.EOF {
				err = io.ErrUnexpectedEOF
			}
			return "", err
		} else if n != 1 {
			panic("invalid state")
		}
		if b[0] == 0 {
			return string(str), nil
		} else {
			str = append(str, b[0])
		}
	}
}

func (f *DebugFile) ReadCStringFromAddress(addr uint32, expectedPrefix, expectedSection string) (string, error) {
	syms := f.findSymbols(uint64(addr))
	if len(syms) == 0 {
		return "", fmt.Errorf("no symbol found for addr=0x%08x in ELF", addr)
	} else if len(syms) > 1 {
		return "", fmt.Errorf("duplicate symbols for addr=0x%08x in ELF", addr)
	}
	sym := syms[0]
	if !strings.HasPrefix(sym.Name, expectedPrefix) {
		return "", fmt.Errorf("invalid name: %q; does not contain %q", sym.Name, expectedPrefix)
	}
	return f.ReadCStringFrom(sym, expectedSection)
}

func (f *DebugFile) DecodeMetadata(uid uint32) (*MessageMetadata, error) {
	syms := f.findSymbols(uint64(uid))
	if len(syms) == 0 {
		return nil, nil
	} else if len(syms) > 1 {
		return nil, fmt.Errorf("duplicate symbols for uid=0x%08x in ELF: %v", uid, syms)
	}
	sym := syms[0]
	if !strings.HasPrefix(sym.Name, "_msg_metadata") {
		return nil, fmt.Errorf("invalid name: %q", sym.Name)
	}
	var metadata struct{ LogLevel, FormatPtr, FilenamePtr, LineNumber uint32 }
	if err := f.ReadSymbolInto(sym, ".debugf_messages", &metadata); err != nil {
		return nil, err
	}
	level, err := ParseLogLevel(metadata.LogLevel)
	if err != nil {
		return nil, err
	}
	fmtStr, err := f.ReadCStringFromAddress(metadata.FormatPtr, "_msg_format", ".debugf_messages")
	if err != nil {
		return nil, err
	}
	filename, err := f.ReadCStringFromAddress(metadata.FilenamePtr, "_msg_filename", ".debugf_messages")
	if err != nil {
		return nil, err
	}
	format, err := ParseFormat(fmtStr)
	if err != nil {
		return nil, err
	}
	return &MessageMetadata{
		MessageUID: uid,
		LogLevel:   level,
		Format:     format,
		Filename:   filename,
		LineNum:    metadata.LineNumber,
	}, nil
}

type DebugData struct {
	files         []*DebugFile
	JunkData      *MessageMetadata
	InvalidRecord *MessageMetadata
	RecordTypes   map[uint32]*MessageMetadata
}

func LoadDebugData(paths []string) (*DebugData, error) {
	dd := &DebugData{}
	for _, path := range paths {
		ldf, err := LoadDebugFile(path)
		if err != nil {
			return nil, err
		}
		dd.files = append(dd.files, ldf)
	}
	formatJunk, err := ParseFormat("Junk Data: '%s'")
	if err != nil {
		return nil, err
	}
	dd.JunkData = &MessageMetadata{
		MessageUID: JunkDataUID,
		Format:     formatJunk,
		Filename:   "<unknown>",
		LineNum:    0,
		LogLevel:   LogCritical,
	}
	formatInvalid, err := ParseFormat("Invalid Record with UID=%08x: '%s'")
	if err != nil {
		return nil, err
	}
	dd.InvalidRecord = &MessageMetadata{
		MessageUID: InvalidRecordUID,
		Format:     formatInvalid,
		Filename:   "<unknown>",
		LineNum:    0,
		LogLevel:   LogCritical,
	}
	dd.RecordTypes = map[uint32]*MessageMetadata{
		JunkDataUID:      nil,
		InvalidRecordUID: nil,
	}
	return dd, nil
}

func (dd *DebugData) LookupMetadata(uid uint32) (*MessageMetadata, error) {
	mm, found := dd.RecordTypes[uid]
	if !found {
		for _, df := range dd.files {
			dm, err := df.DecodeMetadata(uid)
			if err != nil {
				return nil, err
			} else if dm != nil && mm != nil {
				return nil, fmt.Errorf("duplicate metadata for uid=0x%08x", uid)
			} else if dm != nil {
				mm = dm
			}
		}
		dd.RecordTypes[uid] = mm
	}
	return mm, nil
}

// Decode always returns a record, but sometimes also returns an error
func (dd *DebugData) Decode(record []byte) (Record, error) {
	if len(record) < 12 {
		// checked in ParseStream
		panic("records must always be at least twelve bytes (one pointer and one timestamp) long")
	}
	messageUID := binary.LittleEndian.Uint32(record[0:4])
	timestamp, _ := model.FromNanoseconds(binary.LittleEndian.Uint64(record[4:12]))
	metadata, err := dd.LookupMetadata(messageUID)
	if metadata != nil {
		data, recErr := metadata.ParseRecord(record[12:])
		if recErr == nil {
			return Record{
				Metadata:     metadata,
				ArgumentData: data,
				Timestamp:    timestamp,
			}, nil
		}
		err = recErr
	}
	return Record{
		Metadata:     dd.InvalidRecord,
		ArgumentData: []interface{}{messageUID, hex.EncodeToString(record[12:])},
		Timestamp:    timestamp,
	}, err
}
