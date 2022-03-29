package readlog

import (
	"bufio"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"image/color"
	"io"
	"os"
	"path"
	"strings"
)

func Render(record Record) string {
	return record.Metadata.Format.Render(record.ArgumentData)
}

// LogColorRGB is for chart code, but it's kept here so that it can be synchronized with LogColor
func LogColorRGB(level LogLevel) color.RGBA {
	switch level {
	case LogCritical:
		return color.RGBA{205, 0, 0, 255}
	case LogWarning:
		return color.RGBA{205, 205, 0, 255}
	case LogInfo:
		return color.RGBA{205, 0, 205, 255}
	case LogDebug:
		return color.RGBA{0, 205, 0, 255}
	case LogTrace:
		return color.RGBA{0, 0, 238, 255}
	default:
		return color.RGBA{229, 229, 229, 255}
	}
}

func LogColor(level LogLevel, msg string) string {
	switch level {
	case LogCritical:
		return "\033[1;31m" + msg + "\033[0m"
	case LogWarning:
		return "\033[1;33m" + msg + "\033[0m"
	case LogInfo:
		return "\033[1;35m" + msg + "\033[0m"
	case LogDebug:
		return "\033[1;32m" + msg + "\033[0m"
	case LogTrace:
		return "\033[1;34m" + msg + "\033[0m"
	default:
		return msg
	}
}

func GetLines(filename string, lineNum uint32, count uint32) ([]string, error) {
	f, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer func() {
		// reading, so no need for error checking
		_ = f.Close()
	}()
	br := bufio.NewScanner(f)
	var curLine uint32
	var lines []string
	for br.Scan() {
		curLine += 1
		if curLine >= lineNum {
			lines = append(lines, br.Text())
			if curLine >= lineNum+count-1 {
				return lines, nil
			}
		}
	}
	err = br.Err()
	if err == nil {
		if len(lines) >= 1 {
			return lines, nil
		} else {
			return nil, io.EOF
		}
	}
	return nil, err
}

func Renderer(input <-chan Record, output io.Writer, srcPath string, full bool) error {
	bw := bufio.NewWriter(output)
	for line := range input {
		var err error
		rendered := Render(line)
		var text string
		if full {
			if line.Metadata.StableID != "" {
				rendered = line.Metadata.StableID + " - " + rendered
			}
			text = fmt.Sprintf(
				"%08X - %-25s - %15v - %5v - %s",
				line.Metadata.MessageUID,
				fmt.Sprintf("%s:%d", line.Metadata.Filename, line.Metadata.LineNum),
				line.Timestamp, line.Metadata.LogLevel, rendered,
			)
		} else {
			filename := path.Base(line.Metadata.Filename)
			if strings.HasSuffix(filename, ".c") {
				filename = filename[:len(filename)-2]
			}
			filename = strings.ToUpper(filename)
			text = fmt.Sprintf(
				"[%15v] [%14s] [%5v] %s",
				line.Timestamp, filename, line.Metadata.LogLevel, rendered,
			)
		}
		_, err = bw.WriteString(LogColor(line.Metadata.LogLevel, text) + "\n")
		if err != nil {
			return err
		}
		if srcPath != "" && line.Metadata.StableID == "Assertion" && line.Metadata.LineNum != 0 {
			firstLine := line.Metadata.LineNum - 5
			var countLines uint32 = 11
			if line.Metadata.LineNum <= 5 {
				firstLine = 1
				countLines = line.Metadata.LineNum + 5
			}
			filePath := line.Metadata.Filename
			shortPath := line.Metadata.Filename
			if path.IsAbs(filePath) {
				shortPath = path.Join("...", path.Base(path.Dir(filePath)), path.Base(filePath))
			} else {
				filePath = path.Join(srcPath, filePath)
			}
			lines, sourceError := GetLines(filePath, firstLine, countLines)
			var msgs []string
			if sourceError != nil || len(lines) == 0 {
				msgs = []string{
					fmt.Sprintf("CANNOT ACCESS SOURCE %s:%d ==> %v",
						filePath, line.Metadata.LineNum, sourceError),
				}
			} else {
				for i, lineText := range lines {
					msg := fmt.Sprintf("%30s %s",
						fmt.Sprintf("[%s:%d]", shortPath, firstLine+uint32(i)),
						lineText,
					)
					if firstLine+uint32(i) == line.Metadata.LineNum {
						msg = LogColor(line.Metadata.LogLevel, msg)
					}
					msgs = append(msgs, msg)
				}
			}
			for _, msg := range msgs {
				_, err = bw.WriteString(msg + "\n")
				if err != nil {
					return err
				}
			}
		}
		err = bw.Flush()
		if err != nil {
			return err
		}
	}
	return nil
}

func Filter(input <-chan Record, output chan<- Record, minLevel LogLevel, earliest, latest model.VirtualTime) {
	go func() {
		defer close(output)
		for record := range input {
			if record.Metadata.LogLevel > minLevel {
				continue
			}
			if earliest.TimeExists() && record.Timestamp.TimeExists() && record.Timestamp.Before(earliest) {
				continue
			}
			if latest.TimeExists() && record.Timestamp.TimeExists() && record.Timestamp.After(latest) {
				continue
			}
			output <- record
		}
	}()
}
