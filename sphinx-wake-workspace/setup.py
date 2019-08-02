from setuptools import setup, find_packages

long_desc = '''
This package is a Wake domain for Sphinx.
'''

requires = ['Sphinx>=0.6',
            'sphinxcontrib-domaintools>=0.1']

setup(
    name='sphinx-wake',
    version='0.1',
    author='John Andrews',
    description='Wake domain for Sphinx',
    long_description=long_desc,
    zip_safe=False,
    platforms='any',
    packages=find_packages(exclude=['sample*']),
    include_package_data=False,
    install_requires=requires,
    namespace_packages=['wake']
)
