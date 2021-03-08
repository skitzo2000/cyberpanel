# -*- coding: utf-8 -*-


from django.db import models
from packages.models import Package
from loginSystem.models import Administrator
from datetime import datetime

# Create your models here.

class Websites(models.Model):
    admin = models.ForeignKey(Administrator, on_delete=models.PROTECT)
    package = models.ForeignKey(Package, on_delete=models.PROTECT)
    domain = models.CharField(max_length=50,unique=True)
    adminEmail = models.CharField(max_length=50)
    phpSelection = models.CharField(max_length=10)
    ssl = models.IntegerField()
    state = models.IntegerField(default=1)
    externalApp = models.CharField(max_length=30, default=None)

class ChildDomains(models.Model):
    master = models.ForeignKey(Websites,on_delete=models.CASCADE)
    domain = models.CharField(max_length=50, unique=True)
    path = models.CharField(max_length=200,default=None)
    ssl = models.IntegerField()
    phpSelection = models.CharField(max_length=10,default=None)

class Backups(models.Model):
    website = models.ForeignKey(Websites,on_delete=models.CASCADE)
    fileName = models.CharField(max_length=200)
    date = models.CharField(max_length=50)
    size = models.CharField(max_length=50)
    status = models.IntegerField(default=0)

class dest(models.Model):
    destLoc = models.CharField(unique=True,max_length=18)

class backupSchedules(models.Model):
    dest = models.ForeignKey(dest, on_delete=models.CASCADE)
    frequency = models.CharField(max_length=15)

class aliasDomains(models.Model):
    master = models.ForeignKey(Websites, on_delete=models.CASCADE)
    aliasDomain = models.CharField(max_length=75)

class GitLogs(models.Model):
    owner = models.ForeignKey(Websites, on_delete=models.CASCADE)
    date = models.DateTimeField(default=datetime.now, blank=True)
    type = models.CharField(max_length=5)
    message = models.TextField(max_length=65532)

class BackupJob(models.Model):
    logFile = models.CharField(max_length=1000)
    ipAddress = models.CharField(max_length=50)
    port = models.CharField(max_length=15)
    jobSuccessSites = models.IntegerField()
    jobFailedSites = models.IntegerField()
    location = models.IntegerField()

class BackupJobLogs(models.Model):
    owner = models.ForeignKey(BackupJob, on_delete=models.CASCADE)
    status = models.IntegerField()
    message = models.TextField()

class GDrive(models.Model):
    owner = models.ForeignKey(Administrator, on_delete=models.CASCADE)
    name = models.CharField(max_length=50, unique=True)
    auth = models.TextField(max_length=65532, default='Inactive')
    runTime = models.CharField(max_length=20, default='NEVER')

class GDriveSites(models.Model):
    owner = models.ForeignKey(GDrive, on_delete=models.CASCADE)
    domain = models.CharField(max_length=200)

class GDriveJobLogs(models.Model):
    owner = models.ForeignKey(GDrive, on_delete=models.CASCADE)
    status = models.IntegerField()
    message = models.TextField()


### Normal backup models

class NormalBackupDests(models.Model):
    name = models.CharField(max_length=25)
    config = models.TextField()

class NormalBackupJobs(models.Model):
    owner = models.ForeignKey(NormalBackupDests, on_delete=models.CASCADE)
    name = models.CharField(max_length=25)
    config = models.TextField()

class NormalBackupSites(models.Model):
    owner = models.ForeignKey(NormalBackupJobs, on_delete=models.CASCADE)
    domain = models.ForeignKey(Websites, on_delete=models.CASCADE)


class NormalBackupJobLogs(models.Model):
    owner = models.ForeignKey(NormalBackupJobs, on_delete=models.CASCADE)
    status = models.IntegerField()
    message = models.TextField()
