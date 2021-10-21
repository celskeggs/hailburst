import csv

lines = []

header = ["Type", "ID", "Timestamp (ns)", "Total Errors"]

with open("experiment-out.csv", "r") as iterdata:
    iterlines = csv.reader(iterdata)
    assert next(iterlines) == ["Record ID", "Timestamp", "Iteration", "Total Errors", "Marker Returned",
                               "Last Marker Sent"]
    for line in iterlines:
        lines.append(["Iteration"] + line[0:2] + line[3:4])

with open("injections.csv", "r") as injdata:
    injlines = csv.reader(injdata)
    assert next(injlines) == ["Injection #", "Injection Time", "Address", "Old Value", "New Value"]
    for line in injlines:
        lines.append(["Injection"] + line[0:2])

lines.sort(key=lambda x: int(x[2]))

with open("experiment-processed.csv", "w") as out:
    co = csv.writer(out)
    co.writerow(header)
    for line in lines:
        co.writerow(line)
