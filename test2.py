import matplotlib.pyplot as plt

# Function to read data from a file
def read_data(file_path):
    with open(file_path, 'r') as file:
        lines = file.readlines()

    tasks_data = [lines[i:i + 5] for i in range(1, len(lines), 5)]
    
    return tasks_data

schedulers = ['dyn', 'cfs', 'fifo']
file_paths = ['preemptiveWorkload_100_dyn', 'preemptiveWorkload_100_cfs', 'preemptiveWorkload_100_fifo']

# Lists to store average service times for each scheduler
average_service_times = [[] for _ in schedulers]

for scheduler, file_path in zip(schedulers, file_paths):
    tasks_data = read_data('results/preemptiveWorkload/'+file_path)

    print(tasks_data[:5])

    # # Loop through different number of tasks
    # for i in range(1, 101):
    #     avg_service_time = calculate_average_service_time(tasks_data[:i])
    #     average_service_times[schedulers.index(scheduler)].append(avg_service_time)

