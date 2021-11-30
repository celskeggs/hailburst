package readlog

import (
	"bufio"
	"fmt"
	"image/color"
	"io"
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

func Renderer(input <-chan Record, output io.Writer, minLevel LogLevel, full bool) error {
	bw := bufio.NewWriter(output)
	for line := range input {
		if line.Metadata.LogLevel > minLevel {
			continue
		}
		var err error
		var text string
		if full {
			text = fmt.Sprintf(
				"%08X - %-25s - %15v - %5v - %s",
				line.Metadata.MessageUID,
				fmt.Sprintf("%s:%d", line.Metadata.Filename, line.Metadata.LineNum),
				line.Timestamp, line.Metadata.LogLevel, Render(line),
			)
		} else {
			filename := path.Base(line.Metadata.Filename)
			if strings.HasSuffix(filename, ".c") {
				filename = filename[:len(filename)-2]
			}
			filename = strings.ToUpper(filename)
			text = fmt.Sprintf(
				"[%13v] [%12s] [%5v] %s",
				line.Timestamp, filename, line.Metadata.LogLevel, Render(line),
			)
		}
		_, err = bw.WriteString(LogColor(line.Metadata.LogLevel, text) + "\n")
		if err != nil {
			return err
		}
		err = bw.Flush()
		if err != nil {
			return err
		}
	}
	return nil
}
